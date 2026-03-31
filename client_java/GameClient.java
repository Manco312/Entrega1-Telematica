import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Base64;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * CyberGame Client — Java (interfaz web compartida)
 * ==================================================
 * Reemplaza la GUI Swing por la interfaz web alojada en client_web/.
 *
 * Este programa:
 *   1. Sirve los archivos estáticos de client_web/ en HTTP (puerto httpPort, por defecto 8087).
 *   2. Abre un servidor WebSocket en (httpPort+1) que actúa de puente hacia el servidor TCP.
 *   3. Abre el navegador automáticamente.
 *
 * El frontend JS (game.js) detecta el puerto WebSocket como HTTP+1 y se conecta
 * sin necesidad de configuración adicional.
 *
 * Compilación:
 *   javac client_java/GameClient.java
 *
 * Uso:
 *   java -cp client_java GameClient <host> <puerto> [httpPort]
 *   java -cp client_java GameClient localhost 8082
 *
 * Usuarios de prueba:
 *   attacker1 / pass123  (ATTACKER)
 *   defender1 / pass456  (DEFENDER)
 */
public class GameClient {

    static final int    DEFAULT_HTTP_PORT = 8087;
    static final String WS_GUID           = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    /** Directorio raíz de los archivos web estáticos. */
    static volatile Path webRoot;

    // =========================================================================
    // MAIN
    // =========================================================================
    public static void main(String[] args) throws Exception {
        String gameHost = args.length > 0 ? args[0] : "localhost";
        int    gamePort = args.length > 1 ? Integer.parseInt(args[1]) : 8082;
        int    httpPort = args.length > 2 ? Integer.parseInt(args[2]) : DEFAULT_HTTP_PORT;
        int    wsPort   = httpPort + 1;

        // Localizar client_web/ relativo al directorio de la clase compilada
        URI    classUri  = GameClient.class.getProtectionDomain()
                                           .getCodeSource().getLocation().toURI();
        Path   classDir  = Paths.get(classUri);
        webRoot = classDir.getParent().resolve("../client_web").normalize();
        if (!Files.isDirectory(webRoot)) {
            // Fallback: relativo al directorio de trabajo actual
            webRoot = Paths.get("client_web").toAbsolutePath();
        }

        System.out.println("[HTTP]  http://localhost:" + httpPort);
        System.out.println("[WS]    ws://localhost:"   + wsPort);
        System.out.println("[GAME]  " + gameHost + ":" + gamePort);
        System.out.println("[WEB]   " + webRoot);

        startHttpServer(httpPort);
        startWsServer(wsPort, gameHost, gamePort);
        openBrowser("http://localhost:" + httpPort);

        Thread.currentThread().join(); // mantener vivo
    }

    // =========================================================================
    // HTTP — servidor de archivos estáticos (HTTP/1.0 sobre raw socket)
    // =========================================================================
    static void startHttpServer(int port) {
        ExecutorService pool = Executors.newCachedThreadPool();
        new Thread(() -> {
            try (ServerSocket ss = new ServerSocket(port)) {
                while (true) {
                    final Socket client = ss.accept();
                    pool.submit(() -> handleHttpClient(client));
                }
            } catch (IOException e) {
                System.err.println("[!] HTTP error: " + e.getMessage());
            }
        }, "http-accept").start();
    }

    static void handleHttpClient(Socket sock) {
        try (sock) {
            InputStream  in  = sock.getInputStream();
            OutputStream out = sock.getOutputStream();

            // Leer cabeceras HTTP byte a byte hasta \r\n\r\n
            StringBuilder req = new StringBuilder();
            int b, prev1 = -1, prev2 = -1, prev3 = -1;
            while ((b = in.read()) != -1) {
                req.append((char) b);
                if (prev3 == '\r' && prev2 == '\n' && prev1 == '\r' && b == '\n') break;
                prev3 = prev2; prev2 = prev1; prev1 = b;
            }

            String[] lines = req.toString().split("\r\n");
            if (lines.length == 0) return;
            String[] parts = lines[0].split(" ");
            if (parts.length < 2) return;

            // Ignorar query string
            String rawPath = parts[1].contains("?") ? parts[1].substring(0, parts[1].indexOf('?')) : parts[1];
            String path    = "/".equals(rawPath) ? "/index.html" : rawPath;

            Path file = webRoot.resolve(path.substring(1)).normalize();
            if (!file.startsWith(webRoot) || !Files.isRegularFile(file)) {
                byte[] body = "404 Not Found".getBytes(StandardCharsets.UTF_8);
                String resp = "HTTP/1.0 404 Not Found\r\nContent-Length: " + body.length + "\r\n\r\n";
                out.write(resp.getBytes(StandardCharsets.UTF_8));
                out.write(body);
            } else {
                byte[] body = Files.readAllBytes(file);
                String ct   = contentType(file.toString());
                String hdr  = "HTTP/1.0 200 OK\r\nContent-Type: " + ct
                            + "\r\nContent-Length: " + body.length + "\r\n\r\n";
                out.write(hdr.getBytes(StandardCharsets.UTF_8));
                out.write(body);
            }
            out.flush();
        } catch (IOException ignored) {}
    }

    static String contentType(String path) {
        if (path.endsWith(".html")) return "text/html; charset=utf-8";
        if (path.endsWith(".css"))  return "text/css; charset=utf-8";
        if (path.endsWith(".js"))   return "application/javascript; charset=utf-8";
        if (path.endsWith(".json")) return "application/json";
        if (path.endsWith(".png"))  return "image/png";
        if (path.endsWith(".ico"))  return "image/x-icon";
        return "application/octet-stream";
    }

    // =========================================================================
    // WebSocket — servidor de bridge WS ↔ TCP
    // =========================================================================
    static void startWsServer(int wsPort, String gameHost, int gamePort) {
        ExecutorService pool = Executors.newCachedThreadPool();
        new Thread(() -> {
            try (ServerSocket ss = new ServerSocket(wsPort)) {
                while (true) {
                    final Socket client = ss.accept();
                    pool.submit(() -> handleWsClient(client, gameHost, gamePort));
                }
            } catch (IOException e) {
                System.err.println("[!] WS error: " + e.getMessage());
            }
        }, "ws-accept").start();
    }

    static void handleWsClient(Socket wsSocket, String gameHost, int gamePort) {
        try {
            InputStream  wsIn  = wsSocket.getInputStream();
            OutputStream wsOut = wsSocket.getOutputStream();

            // Handshake WebSocket (RFC 6455)
            if (!doHandshake(wsIn, wsOut)) {
                wsSocket.close();
                return;
            }

            // Conectar al servidor de juego (resolución DNS, sin IP hardcodeada)
            Socket tcpSocket;
            try {
                InetAddress addr = InetAddress.getByName(gameHost);
                tcpSocket = new Socket();
                tcpSocket.connect(new InetSocketAddress(addr, gamePort), 6000);
            } catch (Exception e) {
                sendWsText(wsOut, "__PROXY_ERROR__ " + e.getMessage());
                wsSocket.close();
                return;
            }

            InputStream  tcpIn  = tcpSocket.getInputStream();
            OutputStream tcpOut = tcpSocket.getOutputStream();

            // Hilo: TCP → WebSocket
            Thread tcpToWs = new Thread(() -> {
                try {
                    BufferedReader br = new BufferedReader(
                            new InputStreamReader(tcpIn, StandardCharsets.UTF_8));
                    String line;
                    while ((line = br.readLine()) != null) {
                        synchronized (wsOut) { sendWsText(wsOut, line); }
                    }
                } catch (IOException ignored) {
                } finally { closeQuietly(wsSocket); }
            }, "tcp-to-ws");
            tcpToWs.setDaemon(true);
            tcpToWs.start();

            // Hilo principal: WebSocket → TCP
            try {
                PrintWriter pw = new PrintWriter(
                        new OutputStreamWriter(tcpOut, StandardCharsets.UTF_8), true);
                while (true) {
                    byte[] payload = readWsFrame(wsIn);
                    if (payload == null) break;
                    pw.println(new String(payload, StandardCharsets.UTF_8));
                }
            } catch (IOException ignored) {
            } finally {
                closeQuietly(tcpSocket);
                closeQuietly(wsSocket);
            }
        } catch (Exception e) {
            closeQuietly(wsSocket);
        }
    }

    // =========================================================================
    // WebSocket — Handshake (RFC 6455 §4)
    // =========================================================================
    static boolean doHandshake(InputStream in, OutputStream out) throws Exception {
        // Leer cabeceras HTTP byte a byte (evita BufferedReader que consumiría frames)
        StringBuilder sb = new StringBuilder();
        int b, p1 = -1, p2 = -1, p3 = -1;
        while ((b = in.read()) != -1) {
            sb.append((char) b);
            if (p3 == '\r' && p2 == '\n' && p1 == '\r' && b == '\n') break;
            p3 = p2; p2 = p1; p1 = b;
        }

        String wsKey = null;
        for (String line : sb.toString().split("\r\n")) {
            if (line.toLowerCase().startsWith("sec-websocket-key:")) {
                wsKey = line.substring(line.indexOf(':') + 1).trim();
            }
        }
        if (wsKey == null) return false;

        String accept = computeAccept(wsKey);
        String response =
                "HTTP/1.1 101 Switching Protocols\r\n" +
                "Upgrade: websocket\r\n" +
                "Connection: Upgrade\r\n" +
                "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
        out.write(response.getBytes(StandardCharsets.UTF_8));
        out.flush();
        return true;
    }

    static String computeAccept(String key) throws NoSuchAlgorithmException {
        MessageDigest md = MessageDigest.getInstance("SHA-1");
        return Base64.getEncoder().encodeToString(
                md.digest((key + WS_GUID).getBytes(StandardCharsets.UTF_8)));
    }

    // =========================================================================
    // WebSocket — framing (RFC 6455 §5)
    // =========================================================================
    static byte[] readWsFrame(InputStream in) throws IOException {
        int b0 = in.read(), b1 = in.read();
        if (b0 == -1 || b1 == -1) return null;

        int opcode = b0 & 0x0F;
        if (opcode == 8) return null;          // close frame

        boolean masked = (b1 & 0x80) != 0;
        int     len    = b1 & 0x7F;

        if (len == 126) {
            len = ((in.read() & 0xFF) << 8) | (in.read() & 0xFF);
        } else if (len == 127) {
            for (int i = 0; i < 4; i++) in.read(); // descartar 4 bytes altos
            len = ((in.read() & 0xFF) << 24) | ((in.read() & 0xFF) << 16)
                | ((in.read() & 0xFF) << 8)  |  (in.read() & 0xFF);
        }

        byte[] mask    = new byte[4];
        if (masked) readFully(in, mask);

        byte[] payload = new byte[len];
        readFully(in, payload);

        if (masked) {
            for (int i = 0; i < payload.length; i++) payload[i] ^= mask[i % 4];
        }
        return payload;
    }

    static void sendWsText(OutputStream out, String text) throws IOException {
        byte[] payload = text.getBytes(StandardCharsets.UTF_8);
        out.write(0x81);   // FIN + opcode TEXT
        if (payload.length < 126) {
            out.write(payload.length);
        } else if (payload.length < 65536) {
            out.write(126);
            out.write((payload.length >> 8) & 0xFF);
            out.write(payload.length & 0xFF);
        } else {
            out.write(127);
            for (int i = 7; i >= 0; i--) {
                out.write((int) ((payload.length >> (i * 8)) & 0xFF));
            }
        }
        out.write(payload);
        out.flush();
    }

    // =========================================================================
    // Utilidades
    // =========================================================================
    static void readFully(InputStream in, byte[] buf) throws IOException {
        int offset = 0;
        while (offset < buf.length) {
            int n = in.read(buf, offset, buf.length - offset);
            if (n == -1) throw new EOFException("Stream closed mid-frame");
            offset += n;
        }
    }

    static void closeQuietly(Closeable c) {
        try { if (c != null) c.close(); } catch (IOException ignored) {}
    }

    static void openBrowser(String url) {
        System.out.println("[*]     Abriendo " + url + " en el navegador...");
        try {
            if (Desktop.isDesktopSupported() && Desktop.getDesktop().isSupported(Desktop.Action.BROWSE)) {
                Desktop.getDesktop().browse(new URI(url));
                return;
            }
        } catch (Exception ignored) {}
        // Fallback para entornos sin java.awt.Desktop (Linux headless)
        String os = System.getProperty("os.name").toLowerCase();
        try {
            if (os.contains("linux")) {
                Runtime.getRuntime().exec(new String[]{"xdg-open", url});
            } else if (os.contains("mac")) {
                Runtime.getRuntime().exec(new String[]{"open", url});
            } else if (os.contains("win")) {
                Runtime.getRuntime().exec(new String[]{"cmd", "/c", "start", url});
            }
        } catch (Exception ignored) {}
    }
}
