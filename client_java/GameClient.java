import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.io.*;
import java.net.*;
import java.util.*;
import java.util.concurrent.*;

/**
 * CyberGame Client - Java / Swing
 * ================================
 * Cliente del juego con interfaz grafica. Muestra el mapa 20x20 en tiempo
 * real, la posicion de los jugadores y los recursos criticos.
 *
 * Compilacion:
 *   javac GameClient.java
 *
 * Uso:
 *   java GameClient <hostname> <puerto>
 *   Ejemplo: java GameClient localhost 8082
 */
public class GameClient extends JFrame {

    // =====================================================================
    // Constantes
    // =====================================================================
    private static final int MAP_W   = 20;
    private static final int MAP_H   = 20;
    private static final int CELL    = 30;

    private static final Color BG        = new Color(0x0a0a0a);
    private static final Color GRID_CLR  = new Color(0x1a3a1a);
    private static final Color MY_CLR    = new Color(0x00ff41);
    private static final Color ATK_CLR   = new Color(0xff4400);
    private static final Color DEF_CLR   = new Color(0x3399ff);
    private static final Color RES_SAFE  = new Color(0xffff00);
    private static final Color RES_ATK   = new Color(0xff2200);
    private static final Color RES_DEST  = new Color(0x444444);
    private static final Color TEXT_CLR  = new Color(0x00ff41);
    private static final Color PANEL_BG  = new Color(0x111111);
    private static final Color LOG_CLR   = new Color(0x99ffaa);

    // =====================================================================
    // Estado del cliente
    // =====================================================================
    private final String hostname;
    private final int    port;

    private Socket         sock;
    private PrintWriter    out;
    private BufferedReader in;
    private volatile boolean connected = false;

    private String  role    = null;   // "ATTACKER" | "DEFENDER"
    private int[]   pos     = {0, 0};
    private int     roomId  = -1;
    private boolean inGame  = false;

    private final Map<String, int[]> players   = new ConcurrentHashMap<>();
    private final Map<Integer, int[]> resources = new ConcurrentHashMap<>();
    // resource state: 0=SAFE, 1=ATTACKED, 2=DESTROYED
    private final Map<Integer, Integer> resState = new ConcurrentHashMap<>();

    private final BlockingQueue<String> msgQueue = new LinkedBlockingQueue<>();

    // =====================================================================
    // Componentes UI reutilizados
    // =====================================================================
    private MapPanel    mapPanel;
    private JTextArea   logArea;
    private JTextArea   gamesArea;
    private JLabel      posLabel;
    private JLabel      roomLabel;

    // =====================================================================
    // Constructor
    // =====================================================================
    public GameClient(String hostname, int port) {
        this.hostname = hostname;
        this.port     = port;

        setTitle("CyberGame Simulator");
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        setResizable(false);
        getContentPane().setBackground(BG);

        showLogin();
        pack();
        setLocationRelativeTo(null);
        setVisible(true);

        // Timer para procesar mensajes en el hilo de Swing
        new Timer(80, e -> processMessages()).start();
    }

    // =====================================================================
    // Utilitarios de UI
    // =====================================================================
    private static JLabel lbl(String text, Color fg, Font font) {
        JLabel l = new JLabel(text);
        l.setForeground(fg);
        l.setFont(font);
        l.setOpaque(false);
        return l;
    }

    private static JButton btn(String text, Color bg, Color fg) {
        JButton b = new JButton(text);
        b.setBackground(bg);
        b.setForeground(fg);
        b.setFont(new Font("Courier New", Font.BOLD, 11));
        b.setFocusPainted(false);
        b.setBorderPainted(false);
        b.setCursor(Cursor.getPredefinedCursor(Cursor.HAND_CURSOR));
        return b;
    }

    private static JTextField field(int cols) {
        JTextField f = new JTextField(cols);
        f.setBackground(new Color(0x1a1a1a));
        f.setForeground(TEXT_CLR);
        f.setCaretColor(TEXT_CLR);
        f.setFont(new Font("Courier New", Font.PLAIN, 13));
        f.setBorder(BorderFactory.createLineBorder(new Color(0x00ff41), 1));
        return f;
    }

    private void appendLog(String msg) {
        SwingUtilities.invokeLater(() -> {
            if (logArea != null) {
                logArea.append("\u00bb " + msg + "\n");
                logArea.setCaretPosition(logArea.getDocument().getLength());
            }
        });
    }

    // =====================================================================
    // Pantalla: Login
    // =====================================================================
    private void showLogin() {
        getContentPane().removeAll();
        getContentPane().setLayout(new GridBagLayout());

        JPanel card = new JPanel(new GridBagLayout());
        card.setBackground(PANEL_BG);
        card.setBorder(BorderFactory.createLineBorder(TEXT_CLR, 1));
        GridBagConstraints g = new GridBagConstraints();
        g.insets = new Insets(5, 10, 5, 10);
        g.fill   = GridBagConstraints.HORIZONTAL;
        g.gridx  = 0; g.gridy = 0;

        card.add(lbl("[ CYBERSIM ]", TEXT_CLR,
                      new Font("Courier New", Font.BOLD, 22)), g);
        g.gridy++;
        card.add(lbl("Simulador de Ciberseguridad",
                      new Color(0x666666),
                      new Font("Courier New", Font.PLAIN, 10)), g);
        g.gridy++;
        card.add(lbl("Servidor: " + hostname + ":" + port,
                      new Color(0x555555),
                      new Font("Courier New", Font.PLAIN, 9)), g);
        g.gridy++;
        card.add(lbl("Usuario:", new Color(0xaaaaaa),
                      new Font("Courier New", Font.PLAIN, 10)), g);
        g.gridy++;
        JTextField userField = field(22);
        card.add(userField, g);
        g.gridy++;
        card.add(lbl("Contraseña:", new Color(0xaaaaaa),
                      new Font("Courier New", Font.PLAIN, 10)), g);
        g.gridy++;
        JPasswordField passField = new JPasswordField(22);
        passField.setBackground(new Color(0x1a1a1a));
        passField.setForeground(TEXT_CLR);
        passField.setCaretColor(TEXT_CLR);
        passField.setFont(new Font("Courier New", Font.PLAIN, 13));
        passField.setBorder(BorderFactory.createLineBorder(TEXT_CLR, 1));
        card.add(passField, g);

        g.gridy++;
        JLabel statusLbl = lbl("", new Color(0x888888),
                                 new Font("Courier New", Font.PLAIN, 9));
        card.add(statusLbl, g);

        g.gridy++;
        JButton loginBtn = btn(">> CONECTAR", TEXT_CLR, BG);
        card.add(loginBtn, g);

        Runnable doLogin = () -> {
            String user = userField.getText().trim();
            String pass = new String(passField.getPassword()).trim();
            if (user.isEmpty() || pass.isEmpty()) {
                statusLbl.setText("Ingresa usuario y contraseña.");
                return;
            }
            statusLbl.setText("Conectando...");
            new Thread(() -> {
                if (connect()) {
                    sendMsg("LOGIN " + user + " " + pass);
                } else {
                    SwingUtilities.invokeLater(() ->
                        statusLbl.setText("Fallo la conexion."));
                }
            }).start();
        };

        loginBtn.addActionListener(e -> doLogin.run());
        passField.addActionListener(e -> doLogin.run());
        userField.addActionListener(e -> doLogin.run());

        getContentPane().add(card);
        pack();
        userField.requestFocus();
        revalidate(); repaint();
    }

    // =====================================================================
    // Pantalla: Lobby
    // =====================================================================
    private void showLobby() {
        SwingUtilities.invokeLater(() -> {
            getContentPane().removeAll();
            getContentPane().setLayout(new BorderLayout(8, 8));
            getContentPane().setBackground(BG);

            Color roleClr = "ATTACKER".equals(role) ? ATK_CLR : DEF_CLR;

            JPanel top = new JPanel(new FlowLayout(FlowLayout.LEFT, 8, 4));
            top.setBackground(BG);
            top.add(lbl("[ CYBERSIM ] — " + role, roleClr,
                         new Font("Courier New", Font.BOLD, 15)));
            roomLabel = new JLabel("Sala: —");
            roomLabel.setForeground(new Color(0x666666));
            roomLabel.setFont(new Font("Courier New", Font.PLAIN, 9));
            top.add(roomLabel);
            getContentPane().add(top, BorderLayout.NORTH);

            JPanel center = new JPanel(new BorderLayout(4, 4));
            center.setBackground(BG);

            // Botones de accion
            JPanel btnP = new JPanel(new FlowLayout(FlowLayout.LEFT, 4, 2));
            btnP.setBackground(BG);
            JButton bList   = btn("Listar partidas",  PANEL_BG, TEXT_CLR);
            JButton bCreate = btn("Crear partida",    PANEL_BG, TEXT_CLR);
            JButton bJoin   = btn("Unirse a sala",    PANEL_BG, TEXT_CLR);
            JButton bStart  = btn("Iniciar partida",  TEXT_CLR, BG);
            btnP.add(bList); btnP.add(bCreate); btnP.add(bJoin); btnP.add(bStart);
            center.add(btnP, BorderLayout.NORTH);

            gamesArea = new JTextArea(6, 40);
            gamesArea.setEditable(false);
            gamesArea.setBackground(PANEL_BG);
            gamesArea.setForeground(TEXT_CLR);
            gamesArea.setFont(new Font("Courier New", Font.PLAIN, 9));
            gamesArea.setBorder(BorderFactory.createEmptyBorder(4,4,4,4));
            center.add(new JScrollPane(gamesArea), BorderLayout.CENTER);

            logArea = new JTextArea(8, 40);
            logArea.setEditable(false);
            logArea.setBackground(new Color(0x050505));
            logArea.setForeground(LOG_CLR);
            logArea.setFont(new Font("Courier New", Font.PLAIN, 9));
            logArea.setBorder(BorderFactory.createEmptyBorder(4,4,4,4));
            center.add(new JScrollPane(logArea), BorderLayout.SOUTH);

            getContentPane().add(center, BorderLayout.CENTER);

            bList.addActionListener  (e -> sendMsg("LIST_GAMES"));
            bCreate.addActionListener(e -> sendMsg("CREATE_GAME"));
            bJoin.addActionListener  (e -> {
                String rid = JOptionPane.showInputDialog(this, "ID de la sala:");
                if (rid != null && !rid.trim().isEmpty())
                    sendMsg("JOIN_GAME " + rid.trim());
            });
            bStart.addActionListener (e -> sendMsg("START_GAME"));

            sendMsg("LIST_GAMES");
            pack();
            revalidate(); repaint();
        });
    }

    // =====================================================================
    // Pantalla: Juego
    // =====================================================================
    private void showGame() {
        SwingUtilities.invokeLater(() -> {
            getContentPane().removeAll();
            getContentPane().setLayout(new BorderLayout(8, 8));
            getContentPane().setBackground(BG);

            setTitle("CyberGame — " + role + " — Sala " + roomId);

            // Panel izquierdo: mapa
            JPanel leftP = new JPanel(new BorderLayout(2, 2));
            leftP.setBackground(BG);

            mapPanel = new MapPanel();
            leftP.add(mapPanel, BorderLayout.CENTER);

            JPanel infoP = new JPanel(new FlowLayout(FlowLayout.LEFT, 6, 2));
            infoP.setBackground(BG);
            Color roleClr = "ATTACKER".equals(role) ? ATK_CLR : DEF_CLR;
            infoP.add(lbl("Rol: ", new Color(0xaaaaaa),
                           new Font("Courier New", Font.PLAIN, 9)));
            infoP.add(lbl(role, roleClr,
                           new Font("Courier New", Font.BOLD, 9)));
            posLabel = new JLabel("Pos: (0,0)");
            posLabel.setForeground(new Color(0x888888));
            posLabel.setFont(new Font("Courier New", Font.PLAIN, 9));
            infoP.add(posLabel);
            leftP.add(infoP, BorderLayout.SOUTH);

            getContentPane().add(leftP, BorderLayout.CENTER);

            // Panel derecho: controles + log
            JPanel rightP = new JPanel();
            rightP.setLayout(new BoxLayout(rightP, BoxLayout.Y_AXIS));
            rightP.setBackground(BG);
            rightP.setBorder(BorderFactory.createEmptyBorder(4, 8, 4, 8));

            // Movimiento
            rightP.add(lbl("MOVIMIENTO", TEXT_CLR,
                            new Font("Courier New", Font.BOLD, 10)));
            JPanel moveP = new JPanel(new GridLayout(3, 3, 2, 2));
            moveP.setBackground(BG);
            moveP.setMaximumSize(new Dimension(150, 90));

            JButton bN = btn("▲N", PANEL_BG, TEXT_CLR);
            JButton bW = btn("◄W", PANEL_BG, TEXT_CLR);
            JButton bE = btn("E►", PANEL_BG, TEXT_CLR);
            JButton bS = btn("▼S", PANEL_BG, TEXT_CLR);

            moveP.add(new JLabel()); moveP.add(bN); moveP.add(new JLabel());
            moveP.add(bW);          moveP.add(new JLabel()); moveP.add(bE);
            moveP.add(new JLabel()); moveP.add(bS); moveP.add(new JLabel());

            bN.addActionListener(e -> sendMove("NORTH"));
            bS.addActionListener(e -> sendMove("SOUTH"));
            bW.addActionListener(e -> sendMove("WEST"));
            bE.addActionListener(e -> sendMove("EAST"));
            rightP.add(moveP);

            // Teclas
            getRootPane().getInputMap(JComponent.WHEN_IN_FOCUSED_WINDOW).put(
                    KeyStroke.getKeyStroke(KeyEvent.VK_UP, 0), "north");
            getRootPane().getInputMap(JComponent.WHEN_IN_FOCUSED_WINDOW).put(
                    KeyStroke.getKeyStroke(KeyEvent.VK_DOWN, 0), "south");
            getRootPane().getInputMap(JComponent.WHEN_IN_FOCUSED_WINDOW).put(
                    KeyStroke.getKeyStroke(KeyEvent.VK_LEFT, 0), "west");
            getRootPane().getInputMap(JComponent.WHEN_IN_FOCUSED_WINDOW).put(
                    KeyStroke.getKeyStroke(KeyEvent.VK_RIGHT, 0), "east");
            getRootPane().getInputMap(JComponent.WHEN_IN_FOCUSED_WINDOW).put(
                    KeyStroke.getKeyStroke('w'), "north");
            getRootPane().getInputMap(JComponent.WHEN_IN_FOCUSED_WINDOW).put(
                    KeyStroke.getKeyStroke('s'), "south");
            getRootPane().getInputMap(JComponent.WHEN_IN_FOCUSED_WINDOW).put(
                    KeyStroke.getKeyStroke('a'), "west");
            getRootPane().getInputMap(JComponent.WHEN_IN_FOCUSED_WINDOW).put(
                    KeyStroke.getKeyStroke('d'), "east");
            getRootPane().getActionMap().put("north", quickAction(() -> sendMove("NORTH")));
            getRootPane().getActionMap().put("south", quickAction(() -> sendMove("SOUTH")));
            getRootPane().getActionMap().put("west",  quickAction(() -> sendMove("WEST")));
            getRootPane().getActionMap().put("east",  quickAction(() -> sendMove("EAST")));

            // Acciones
            rightP.add(Box.createVerticalStrut(8));
            rightP.add(lbl("ACCIONES", TEXT_CLR,
                            new Font("Courier New", Font.BOLD, 10)));

            JButton bScan   = btn("SCAN",    PANEL_BG, TEXT_CLR);
            JButton bAction = "ATTACKER".equals(role)
                ? btn("ATTACK",   new Color(0x330000), new Color(0xff6666))
                : btn("MITIGATE", new Color(0x001133), new Color(0x6699ff));
            JButton bStatus = btn("STATUS",  PANEL_BG, TEXT_CLR);
            JButton bQuit   = btn("QUIT",    new Color(0x220000), new Color(0xcc4444));

            for (JButton b : new JButton[]{bScan, bAction, bStatus, bQuit}) {
                b.setMaximumSize(new Dimension(150, 28));
                b.setAlignmentX(Component.LEFT_ALIGNMENT);
                rightP.add(b);
                rightP.add(Box.createVerticalStrut(3));
            }

            bScan.addActionListener(e -> sendMsg("SCAN"));
            if ("ATTACKER".equals(role)) {
                bAction.addActionListener(e -> doAttack());
            } else {
                bAction.addActionListener(e -> doMitigate());
            }
            bStatus.addActionListener(e -> sendMsg("STATUS"));
            bQuit.addActionListener(e -> {
                int r = JOptionPane.showConfirmDialog(this, "¿Salir del juego?",
                        "Salir", JOptionPane.YES_NO_OPTION);
                if (r == JOptionPane.YES_OPTION) {
                    sendMsg("QUIT");
                    disconnect();
                    System.exit(0);
                }
            });

            // Leyenda
            rightP.add(Box.createVerticalStrut(8));
            rightP.add(lbl("LEYENDA", TEXT_CLR,
                            new Font("Courier New", Font.BOLD, 10)));
            Object[][] legend = {
                {"●", MY_CLR,   "Tu jugador"},
                {"●", ATK_CLR,  "Atacante"},
                {"●", DEF_CLR,  "Defensor"},
                {"■", RES_SAFE, "Servidor seguro"},
                {"■", RES_ATK,  "Servidor atacado"},
                {"■", RES_DEST, "Servidor destruido"},
            };
            for (Object[] row : legend) {
                JPanel lp = new JPanel(new FlowLayout(FlowLayout.LEFT, 2, 0));
                lp.setBackground(BG);
                JLabel sym = lbl((String)row[0], (Color)row[1],
                                  new Font("Courier New", Font.PLAIN, 12));
                JLabel desc= lbl(" " + row[2], new Color(0x666666),
                                  new Font("Courier New", Font.PLAIN, 8));
                lp.add(sym); lp.add(desc);
                rightP.add(lp);
            }

            // Log
            rightP.add(Box.createVerticalStrut(8));
            rightP.add(lbl("LOG", TEXT_CLR,
                            new Font("Courier New", Font.BOLD, 10)));
            logArea = new JTextArea(14, 26);
            logArea.setEditable(false);
            logArea.setBackground(new Color(0x050505));
            logArea.setForeground(LOG_CLR);
            logArea.setFont(new Font("Courier New", Font.PLAIN, 8));
            logArea.setLineWrap(true);
            JScrollPane logScroll = new JScrollPane(logArea);
            logScroll.setMaximumSize(new Dimension(210, 280));
            logScroll.setAlignmentX(Component.LEFT_ALIGNMENT);
            rightP.add(logScroll);

            getContentPane().add(rightP, BorderLayout.EAST);

            pack();
            revalidate(); repaint();
        });
    }

    // =====================================================================
    // Panel del mapa (dibujado con paintComponent)
    // =====================================================================
    private class MapPanel extends JPanel {
        MapPanel() {
            setPreferredSize(new Dimension(MAP_W * CELL + 1, MAP_H * CELL + 1));
            setBackground(new Color(0x050505));
            setBorder(BorderFactory.createLineBorder(TEXT_CLR, 1));
        }

        @Override
        protected void paintComponent(Graphics g2d) {
            super.paintComponent(g2d);
            Graphics2D g = (Graphics2D) g2d;
            g.setRenderingHint(RenderingHints.KEY_ANTIALIASING,
                               RenderingHints.VALUE_ANTIALIAS_ON);

            // Grid
            g.setColor(GRID_CLR);
            for (int i = 0; i <= MAP_W; i++) g.drawLine(i*CELL, 0, i*CELL, MAP_H*CELL);
            for (int j = 0; j <= MAP_H; j++) g.drawLine(0, j*CELL, MAP_W*CELL, j*CELL);

            // Recursos
            for (Map.Entry<Integer, int[]> e : resources.entrySet()) {
                int rid = e.getKey();
                int[] rc = e.getValue();
                int st  = resState.getOrDefault(rid, 0);
                Color clr = st == 0 ? RES_SAFE : st == 1 ? RES_ATK : RES_DEST;
                int x1 = rc[0]*CELL+4, y1 = rc[1]*CELL+4;
                int sz = CELL-8;
                g.setColor(clr);
                g.fillRect(x1, y1, sz, sz);
                g.setColor(Color.BLACK);
                g.setFont(new Font("Courier New", Font.BOLD, 9));
                g.drawString("S", x1 + sz/2 - 4, y1 + sz/2 + 4);
            }

            // Otros jugadores
            for (int[] pdata : players.values()) {
                // pdata[0]=x, pdata[1]=y, pdata[2]=0(ATK)/1(DEF)
                Color clr = pdata[2] == 0 ? ATK_CLR : DEF_CLR;
                int cx = pdata[0]*CELL + CELL/2;
                int cy = pdata[1]*CELL + CELL/2;
                int r2 = CELL/2 - 4;
                g.setColor(clr);
                g.fillOval(cx-r2, cy-r2, r2*2, r2*2);
            }

            // Mi jugador
            int cx = pos[0]*CELL + CELL/2;
            int cy = pos[1]*CELL + CELL/2;
            int r2 = CELL/2 - 3;
            g.setColor(MY_CLR);
            g.fillOval(cx-r2, cy-r2, r2*2, r2*2);
            g.setColor(Color.WHITE);
            g.setStroke(new BasicStroke(2));
            g.drawOval(cx-r2, cy-r2, r2*2, r2*2);
            g.setColor(Color.BLACK);
            g.setFont(new Font("Courier New", Font.BOLD, 8));
            g.drawString("U", cx-4, cy+4);
        }
    }

    private AbstractAction quickAction(Runnable r) {
        return new AbstractAction() {
            @Override public void actionPerformed(ActionEvent e) { r.run(); }
        };
    }

    // =====================================================================
    // Red
    // =====================================================================
    private boolean connect() {
        try {
            // DNS resolution: InetAddress.getByName resuelve hostname, no IP hardcodeada
            InetAddress addr = InetAddress.getByName(hostname);
            sock = new Socket();
            sock.connect(new InetSocketAddress(addr, port), 6000);
            out  = new PrintWriter(new BufferedWriter(
                       new OutputStreamWriter(sock.getOutputStream(), "UTF-8")), true);
            in   = new BufferedReader(
                       new InputStreamReader(sock.getInputStream(), "UTF-8"));
            connected = true;
            new Thread(this::recvLoop, "recv-thread").start();
            return true;
        } catch (UnknownHostException e) {
            JOptionPane.showMessageDialog(this,
                "No se pudo resolver '" + hostname + "':\n" + e.getMessage(),
                "Error DNS", JOptionPane.ERROR_MESSAGE);
            return false;
        } catch (Exception e) {
            JOptionPane.showMessageDialog(this,
                "No se pudo conectar:\n" + e.getMessage(),
                "Error", JOptionPane.ERROR_MESSAGE);
            return false;
        }
    }

    private void sendMsg(String msg) {
        if (connected && out != null) {
            out.println(msg);
        }
    }

    private void sendMove(String dir) {
        if (inGame) sendMsg("MOVE " + dir);
    }

    private void disconnect() {
        connected = false;
        try { if (sock != null) sock.close(); } catch (Exception ignored) {}
    }

    private void recvLoop() {
        try {
            String line;
            while (connected && (line = in.readLine()) != null) {
                msgQueue.put(line);
            }
        } catch (Exception e) {
            if (connected) {
                try { msgQueue.put("__DISCONNECTED__"); } catch (Exception ignored) {}
            }
        }
        connected = false;
    }

    private void processMessages() {
        String msg;
        while ((msg = msgQueue.poll()) != null) {
            handleMsg(msg);
        }
    }

    // =====================================================================
    // Procesamiento de mensajes
    // =====================================================================
    private void handleMsg(String msg) {
        appendLog("S: " + msg);

        if ("__DISCONNECTED__".equals(msg)) {
            JOptionPane.showMessageDialog(this,
                "Se perdio la conexion con el servidor.",
                "Desconectado", JOptionPane.WARNING_MESSAGE);
            showLogin();
            return;
        }

        // Autenticacion
        if (msg.startsWith("OK LOGIN role:")) {
            role = msg.split("role:")[1].trim();
            showLobby();

        } else if (msg.startsWith("ERROR")) {
            JOptionPane.showMessageDialog(this, msg,
                "Error", JOptionPane.ERROR_MESSAGE);

        // Lobby
        } else if (msg.startsWith("GAMES ")) {
            if (gamesArea != null) gamesArea.setText(msg);

        } else if (msg.startsWith("OK CREATE room:")) {
            roomId = Integer.parseInt(msg.split("room:")[1].trim());
            if (roomLabel != null) roomLabel.setText("Sala: " + roomId);
            appendLog("Sala " + roomId + " creada.");

        } else if (msg.startsWith("OK JOIN room:")) {
            for (String p : msg.split("\\s+")) {
                if (p.startsWith("room:"))
                    roomId = Integer.parseInt(p.split(":")[1]);
            }
            if (roomLabel != null) roomLabel.setText("Sala: " + roomId);
            appendLog("Unido a sala " + roomId + ".");

        // Inicio de partida
        } else if (msg.startsWith("GAME_START")) {
            handleGameStart(msg);

        // Durante el juego
        } else if (msg.startsWith("OK MOVE pos:")) {
            String[] c = msg.split("pos:")[1].trim().split(",");
            pos[0] = Integer.parseInt(c[0]);
            pos[1] = Integer.parseInt(c[1]);
            if (posLabel != null) posLabel.setText("Pos: (" + pos[0] + "," + pos[1] + ")");
            if (mapPanel != null) mapPanel.repaint();

        } else if (msg.startsWith("PLAYER_MOVED")) {
            handlePlayerMoved(msg);
            if (mapPanel != null) mapPanel.repaint();

        } else if (msg.startsWith("SCAN_RESULT")) {
            if (msg.contains("FOUND")) {
                int rid = Integer.parseInt(msg.split("resource_id:")[1].split("\\s+")[0]);
                String[] c = msg.split("pos:")[1].trim().split(",");
                resources.put(rid, new int[]{Integer.parseInt(c[0]),
                                             Integer.parseInt(c[1])});
                resState.put(rid, 0);
                appendLog("[SCAN] Recurso " + rid + " en (" + c[0] + "," + c[1] + ")");
                if (mapPanel != null) mapPanel.repaint();
            } else {
                appendLog("[SCAN] Celda vacia.");
            }

        } else if (msg.startsWith("NOTIFY_ATTACK")) {
            handleNotifyAttack(msg);
            if (mapPanel != null) mapPanel.repaint();

        } else if (msg.startsWith("NOTIFY_MITIGATED")) {
            int rid = Integer.parseInt(msg.split("resource_id:")[1].split("\\s+")[0]);
            resState.put(rid, 0);
            appendLog("[OK] Recurso " + rid + " mitigado.");
            if (mapPanel != null) mapPanel.repaint();

        } else if (msg.startsWith("NOTIFY_DESTROYED")) {
            int rid = Integer.parseInt(msg.split("resource_id:")[1].trim());
            resState.put(rid, 2);
            appendLog("[!!!] Recurso " + rid + " DESTRUIDO.");
            if (mapPanel != null) mapPanel.repaint();

        } else if (msg.startsWith("NOTIFY_DISCONNECT")) {
            String name = msg.split("player:")[1].trim();
            players.remove(name);
            appendLog("Jugador '" + name + "' se desconecto.");
            if (mapPanel != null) mapPanel.repaint();

        } else if (msg.startsWith("GAME_OVER")) {
            String winner = msg.split("winner:")[1].trim();
            JOptionPane.showMessageDialog(this,
                "Partida terminada.\nGanador: " + winner,
                "FIN DEL JUEGO", JOptionPane.INFORMATION_MESSAGE);
            inGame = false;

        } else if (msg.startsWith("STATUS")) {
            appendLog(msg);
        }
    }

    private void handleGameStart(String msg) {
        inGame = true;
        resources.clear();
        resState.clear();
        players.clear();

        for (String part : msg.split("\\s+")) {
            if (part.startsWith("pos:")) {
                String[] c = part.split(":")[1].split(",");
                pos[0] = Integer.parseInt(c[0]);
                pos[1] = Integer.parseInt(c[1]);
            } else if (part.startsWith("resources:") && !part.contains("?")) {
                String resStr = part.split(":", 2)[1];
                String[] pairs = resStr.split(";");
                for (int i = 0; i < pairs.length; i++) {
                    String[] c = pairs[i].split(",");
                    if (c.length == 2) {
                        resources.put(i, new int[]{Integer.parseInt(c[0]),
                                                    Integer.parseInt(c[1])});
                        resState.put(i, 0);
                    }
                }
            }
        }

        showGame();
        appendLog(">>> PARTIDA INICIADA <<<");
        if ("ATTACKER".equals(role)) {
            appendLog("Muevete y usa SCAN para encontrar servidores.");
        } else {
            appendLog("Defiende los servidores de los ataques!");
        }
    }

    private void handlePlayerMoved(String msg) {
        try {
            String name = null, roleS = null;
            int x = 0, y = 0;
            for (String part : msg.split("\\s+")) {
                if (part.startsWith("player:")) name  = part.split(":")[1];
                else if (part.startsWith("role:"))  roleS = part.split(":")[1];
                else if (part.startsWith("pos:")) {
                    String[] c = part.split(":")[1].split(",");
                    x = Integer.parseInt(c[0]);
                    y = Integer.parseInt(c[1]);
                }
            }
            if (name != null) {
                int roleInt = "ATTACKER".equals(roleS) ? 0 : 1;
                players.put(name, new int[]{x, y, roleInt});
            }
        } catch (Exception ignored) {}
    }

    private void handleNotifyAttack(String msg) {
        try {
            int rid = Integer.parseInt(msg.split("resource_id:")[1].split("\\s+")[0]);
            String[] c = msg.split("pos:")[1].split("\\s+")[0].split(",");
            int rx = Integer.parseInt(c[0]), ry = Integer.parseInt(c[1]);
            String attacker = msg.split("attacker:")[1].split("\\s+")[0];
            String timeout  = msg.split("timeout:")[1].trim();

            if (!resources.containsKey(rid))
                resources.put(rid, new int[]{rx, ry});
            resState.put(rid, 1);

            appendLog("[!! ATAQUE] Recurso " + rid + " por " + attacker
                      + ". " + timeout + "s para mitigar!");

            if ("DEFENDER".equals(role)) {
                JOptionPane.showMessageDialog(this,
                    "Recurso " + rid + " bajo ataque por '" + attacker + "'!\n"
                    + "Ubicacion: (" + rx + "," + ry + ")\n"
                    + "Tienes " + timeout + " segundos.",
                    "ATAQUE", JOptionPane.WARNING_MESSAGE);
            }
        } catch (Exception ignored) {}
    }

    // =====================================================================
    // Acciones de juego
    // =====================================================================
    private void doAttack() {
        if (!inGame) return;
        // Auto-detectar recurso en posicion actual
        for (Map.Entry<Integer, int[]> e : resources.entrySet()) {
            int[] rc = e.getValue();
            if (rc[0] == pos[0] && rc[1] == pos[1]) {
                int r = JOptionPane.showConfirmDialog(this,
                    "Atacar recurso " + e.getKey() + " en " + Arrays.toString(pos) + "?",
                    "Atacar", JOptionPane.YES_NO_OPTION);
                if (r == JOptionPane.YES_OPTION)
                    sendMsg("ATTACK " + e.getKey());
                return;
            }
        }
        String rid = JOptionPane.showInputDialog(this, "ID del recurso a atacar:");
        if (rid != null && !rid.trim().isEmpty())
            sendMsg("ATTACK " + rid.trim());
    }

    private void doMitigate() {
        if (!inGame) return;
        // Auto-detectar recurso atacado en posicion actual
        for (Map.Entry<Integer, int[]> e : resources.entrySet()) {
            int[] rc = e.getValue();
            if (rc[0] == pos[0] && rc[1] == pos[1]
                    && resState.getOrDefault(e.getKey(), 0) == 1) {
                int r = JOptionPane.showConfirmDialog(this,
                    "Mitigar ataque en recurso " + e.getKey() + "?",
                    "Mitigar", JOptionPane.YES_NO_OPTION);
                if (r == JOptionPane.YES_OPTION)
                    sendMsg("MITIGATE " + e.getKey());
                return;
            }
        }
        String rid = JOptionPane.showInputDialog(this, "ID del recurso a mitigar:");
        if (rid != null && !rid.trim().isEmpty())
            sendMsg("MITIGATE " + rid.trim());
    }

    // =====================================================================
    // Main
    // =====================================================================
    public static void main(String[] args) {
        if (args.length < 2) {
            System.err.println("Uso: java GameClient <hostname> <puerto>");
            System.err.println("Ejemplo: java GameClient localhost 8082");
            System.exit(1);
        }

        String hostname = args[0];
        int port;
        try {
            port = Integer.parseInt(args[1]);
        } catch (NumberFormatException e) {
            System.err.println("Error: el puerto debe ser un numero entero.");
            System.exit(1);
            return;
        }

        // Usar look and feel del sistema
        try {
            UIManager.setLookAndFeel(UIManager.getCrossPlatformLookAndFeelClassName());
        } catch (Exception ignored) {}

        final String h = hostname;
        final int   p = port;
        SwingUtilities.invokeLater(() -> new GameClient(h, p));
    }
}
