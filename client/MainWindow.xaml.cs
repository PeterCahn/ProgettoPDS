using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media.Imaging;
using System.Threading;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Diagnostics;
using System.Runtime.InteropServices;
using client;
using System.Data;
using System.ComponentModel;
using System.Windows.Forms;
using System.Text.RegularExpressions;

/* TODO:
 * - Distruttore (utile ad esempio per fare Mutex.Dispose())
 * - Icona con sfondo nero 
 */

namespace WpfApplication1
{
    public partial class MainWindow : Window
    {
        private List<int> comandoDaInviare = new List<int>();
        private string currentConnectedServer;
        private Dictionary<string, ServerInfo> servers = new Dictionary<string, ServerInfo>();        
                
        /* Mutex necessario alla gestione delle modifiche nella listView1 perchè i thread statistics and notifications
         * accedono alla stessa tablesMap[currentConnectedServer]
         */
        private static Mutex tablesMapsEntryMutex = new Mutex();

        public MainWindow()
        {
            InitializeComponent();
            
            // Aggiungi elemento vuoto alla serversListBox
            int index = serversListBox.Items.Add("Nessun server connesso");
            serversListBox.SelectedIndex = index;
            currentConnectedServer = serversListBox.Items[index] as string;

            textBoxIpAddress.Focus();
            
        }
        
        public IPEndPoint parseHostPort(string hostPort)
        {
            /* Match di indirizzi ip:porta del tipo: [0-255].[0-255].[0-255].[0.255]:[1024-65535] */
            Regex hostPortMatch = new Regex(@"^(?<ip>([01]?\d\d?|2[0-4]\d|25[0-5])\.([01]?\d\d?|2[0-4]\d|25[0-5])\.([01]?\d\d?|2[0-4]\d|25[0-5])\.([01]?\d\d?|2[0-4]\d|25[0-5])):(?<port>10[2-9][4-9]|[2-9]\d\d\d|[1-5]\d\d\d\d|6[0-4]\d\d\d|65[0-5]\d\d|655[0-3]\d|6553[0-5])$", RegexOptions.Compiled);
            Match match = hostPortMatch.Match(hostPort);
            if (!match.Success)
                return null;

            return new IPEndPoint(IPAddress.Parse(match.Groups["ip"].Value), int.Parse(match.Groups["port"].Value));
        }

        private void OnKeyDownHandler(object sender, System.Windows.Input.KeyEventArgs e)
        {
            if (e.Key == Key.Return)
            {
                connettiAlServer();
            }
        }

        private void buttonConnetti_Click(object sender, RoutedEventArgs e)
        {
            connettiAlServer();
        }

        private void connettiAlServer()
        {
            try
            {
                tablesMapsEntryMutex.WaitOne();

                // Ricava IPAddress da risultati interrogazione DNS
                IPEndPoint ipPort = parseHostPort(textBoxIpAddress.Text);
                if(ipPort == null){                    
                    System.Windows.MessageBox.Show("Formato ammesso: [0-255].[0-255].[0-255].[0.255]:[1024-65535]");
                    return;
                }
                
                string ipAddress = ipPort.Address.ToString();
                Int32 port = ipPort.Port;

                string serverName = ipAddress + ":" + port;
                                
                if (servers.ContainsKey(serverName))
                {
                    /* Avvisare che il server è gia connesso (popup?) */
                    tablesMapsEntryMutex.ReleaseMutex();
                    return;
                }
                tablesMapsEntryMutex.ReleaseMutex();

                TcpClient server = new TcpClient(ipAddress, port);

                ServerInfo si = new ServerInfo();
                si.server = server;

                // Aggiorna bottoni
                buttonDisconnetti.Visibility = Visibility.Visible;
                textBoxIpAddress.Text = "";                
                buttonCattura.IsEnabled = true;

                // Acquisisci il mutex per aggiungere i dati alla lista dei server
                // prima che i thread creati possano leggere da quella lista
                tablesMapsEntryMutex.WaitOne();

                // Aggiungi MyTable vuota in ServerInfo
                si.table = new MyTable();

                // ManualResetEvent settato a false perché il thread si blocchi quando chiama WaitOne
                // Necessario per far terminare il thread quando si vuole disconnettere quel server
                si.disconnectionEvent = new ManualResetEvent(false);

                // Inizializzazione mutex per proteggere modifiche alla lista delle finestre
                si.tableModificationsMutex = new Mutex();

                // Aggiungi il ServerInfo alla lista dei "servers"
                servers.Add(serverName, si);

                // Lancio dei thread posticipato a quando la chiave "serverName" è effettivamente inserita
                // per evitare che i thread riferissero a una chiave ancora non esistente.
                // Una volta partiti tutto il necessario sarà presente in servers[serverName].
                // I riferimenti dei thread per monitorare l'uscita sono legati direttamente nel ServerInfo alla creazione dei thread stessi
                tablesMapsEntryMutex.ReleaseMutex();

                // Avvia thread per notifiche
                servers[serverName].notificationsThread = new Thread(() => manageNotifications(serverName));      // lambda perchè è necessario anche passare il parametro
                servers[serverName].notificationsThread.IsBackground = true;
                servers[serverName].notificationsThread.Name = "notif_thread_" + serverName;
                servers[serverName].notificationsThread.Start();

                // Avvia thread per statistiche live
                servers[serverName].statisticThread = new Thread(() => manageStatistics(serverName));
                servers[serverName].statisticThread.IsBackground = true;
                servers[serverName].statisticThread.Name = "stats_thread_" + serverName;
                servers[serverName].statisticThread.Start();

                // Mostra la nuova tavola
                listView1.ItemsSource = si.table.Finestre;

                // Aggiungi il nuovo server alla lista di server nella lista combo box
                if (serversListBox.Items[0].Equals("Nessun server connesso")) // se c'è solo l'elemento "Nessun server connesso" (è il primo)
                    serversListBox.Items.RemoveAt(0); // rimuovi primo elemento
                
                int index = serversListBox.Items.Add(serverName);
                serversListBox.SelectedIndex = index;
                currentConnectedServer = serversListBox.Items[index] as string;

                // Cambia la label per mostrare qual è server appena connesso
                indirizzoServerConnesso.Content = serverName;
                listView1.Focus();

            }
            catch (SocketException)
            {
                //textBoxStato.AppendText("ERRORE: Problema riscontrato nella connessione al server. Riprovare.\n");
                return;
            }
            catch (IOException )
            {
                //textBoxStato.AppendText("ECCEZIONE: " + ioe.ToString() + "\n");
                return;
            }
            catch (InvalidOperationException e)
            {
                System.Windows.MessageBox.Show(e.ToString());
            }
            catch (Exception )
            {

            }
        }

        void addItemToListView(Int32 hwnd, string server, string nomeProgramma, BitmapImage bmp)
        {
            // Aggiungi il nuovo elemento all'elenco delle tabelle
            servers[server].table.addFinestra(hwnd, nomeProgramma, "Background", 0, 0, bmp);
        }

        /* Viene eseguito in un thread a parte.
         * Si occupa della gestione delle statistiche, aggiornando le percentuali di Focus ogni 500ms.
         * In questo modo abbiamo statistiche "live"
         */
        private void manageStatistics(string serverName)
        {
            // Imposta tempo connessione
            DateTime connectionTime = DateTime.Now;
            try
            {
                DateTime lastUpdate = DateTime.Now;
                                
                while (true)
                {
                    // il MutualResetEvent disconnectionEvent è usato anche per far attendere il thread nell'aggiornare le statistiche
                    bool isSignaled = servers[serverName].disconnectionEvent.WaitOne(500);
                    if (isSignaled) break;

                    servers[serverName].tableModificationsMutex.WaitOne();
                    
                    foreach (Finestra finestra in servers[serverName].table.Finestre)
                    {
                        if (finestra.StatoFinestra.Equals("Focus"))
                        {
                            finestra.TempoFocus = (DateTime.Now - lastUpdate).TotalMilliseconds + (double)finestra.TempoFocus;
                            lastUpdate = DateTime.Now;
                        }

                        // Calcola la percentuale
                        double perc = ((double)finestra.TempoFocus / (DateTime.Now - connectionTime).TotalMilliseconds * 100);
                        finestra.TempoFocusPerc = Math.Round(perc, 2); // arrotonda la percentuale mostrata a due cifre dopo la virgola
                    }                    
                    
                    servers[serverName].tableModificationsMutex.ReleaseMutex();
                
                }
            }            
            catch (InvalidOperationException e)
            {
                System.Windows.MessageBox.Show("manageStatistics() 1: " + e.ToString());
            }
            catch (Exception exc)
            {
                System.Windows.MessageBox.Show("manageStatistics() 2: " + exc.ToString());
            }
            finally
            {

            }
        }

        /* Viene eseguito in un thread a parte.
         * Si occupa della ricezione e gestione delle notifiche.
         */
        private void manageNotifications(string serverName)
        {
            NetworkStream serverStream;
            byte[] buffer = new byte[7];

            tablesMapsEntryMutex.WaitOne();
            if (!servers.ContainsKey(serverName))
            {
                tablesMapsEntryMutex.ReleaseMutex();
                return;
            }
            TcpClient server = servers[serverName].server;
            tablesMapsEntryMutex.ReleaseMutex();
            
            serverStream = server.GetStream();

            string operation = null;
            string progName = null;                        

            // Vecchia condizione: !((sock.Poll(1000, SelectMode.SelectRead) && (sock.Available == 0)) || !sock.Connected
            // Poll() ritorna true se la connessione è chiusa, resettata, terminata o in attesa (non attiva), oppure se è attiva e ci sono dati da leggere
            // Available() ritorna il numero di dati da leggere
            // Se Available() è 0 e Poll() ritorna true, la connessione è chiusa

            try
            {
                //while(!((servers[serverName].socket.Poll(1000, SelectMode.SelectRead) && (servers[serverName].socket.Available == 0)) || !servers[serverName].socket.Connected))
                while(serverStream.CanRead && server.Connected)
                {
                    // Aspetto se si sta settando il disconnectionEvent, altrimenti leggo i dati ricevuti.                     
                    bool isSignaled = servers[serverName].disconnectionEvent.WaitOne(1);
                    if (!isSignaled)
                    {
                        // Leggi inizio e dimensione messaggio "--<4 byte int>-" = 7 byte in "buffer"
                        int offset = 0;
                        int remaining = 7;
                        while (remaining > 0){
                            int read = serverStream.Read(buffer, offset, remaining);
                            remaining -= read;
                            offset += read;
                        }

                        // Leggi la dimensione del messaggio dal buffer
                        Int32 msgSize = IPAddress.NetworkToHostOrder(BitConverter.ToInt32(buffer, 2));

                        // Leggi tutto il messaggio in "msg" => dimensione "msgSize"
                        byte[] msg = new byte[msgSize];
                        offset = 0;
                        remaining = msgSize;
                        while (remaining > 0){
                            int read = serverStream.Read(msg, offset, remaining);
                            remaining -= read;
                            offset += read;
                        }

                        // Estrai operazione => primi 5 byte
                        byte[] op = new byte[5];
                        Array.Copy(msg, 0, op, 0, 5);
                        operation = Encoding.ASCII.GetString(op);

                        if(operation == "OKCLO")
                        {
                            servers[serverName].disconnectionEvent.Set();
                            continue;
                        }

                        // Estrai hwnd: successivi 5 byte.
                        byte[] h = new byte[5];
                        Array.Copy(msg, 6, h, 0, 4);
                        Int32 hwnd = BitConverter.ToInt32(msg, 6);

                        // Estrai lunghezza nome programma => offset 6 (offset 5 è il '-' che precede)
                        int progNameLength = IPAddress.NetworkToHostOrder(BitConverter.ToInt32(msg, 11));
                        // Leggi nome del programma => da offset 11 (6 di operazione + 5 di dimensione (incluso 1 di trattino))
                        byte[] pN = new byte[progNameLength];
                        Array.Copy(msg, 5 + 5 + 6, pN, 0, progNameLength);
                        progName = Encoding.Unicode.GetString(pN);

                        // TODO: aggiornare descrizione messaggi 
                        /* Possibili valori ricevuti:
                            * --<4 bytes di dimensione messaggio>-FOCUS-<4B per dimensione nome programma>-<nome_nuova_app_focus>
                            * --<4 bytes di dimensione messaggio>-CLOSE-<4B per dimensione nome programma>-<nome_app_chiusa>
                            * --<4 bytes di dimensione messaggio>-OPENP-<4B per dimensione nome programma>-<nome_nuova_app_aperta>-<4B di dimensione icona>-<bitmap>
                            */

                        switch (operation)
                        {
                            case "FOCUS":
                                // Cambia programma col focus
                                servers[serverName].tableModificationsMutex.WaitOne();
                                foreach (Finestra finestra in servers[serverName].table.Finestre)
                                {
                                    if (finestra.Hwnd.Equals(hwnd))
                                        finestra.StatoFinestra = "Focus";
                                    else if (finestra.StatoFinestra.Equals("Focus"))
                                        finestra.StatoFinestra = "Background";
                                }
                                servers[serverName].tableModificationsMutex.ReleaseMutex();

                                break;
                            case "CLOSE":
                                // Rimuovi programma dalla listView
                                servers[serverName].tableModificationsMutex.WaitOne();
                                foreach (Finestra finestra in servers[serverName].table.Finestre)
                                {
                                    if (finestra.Hwnd.Equals(hwnd))
                                    {
                                        // TODO: se % è zero elimina dalla lista, altrimenti setta "Stato finestra" a "Closed"
                                        finestra.StatoFinestra = "Closed";                                        
                                        break;
                                    }
                                }
                                servers[serverName].tableModificationsMutex.ReleaseMutex();

                                break;
                            case "TTCHA":
                                servers[serverName].tableModificationsMutex.WaitOne();
                                foreach (Finestra finestra in servers[serverName].table.Finestre)
                                {
                                    if (finestra.Hwnd.Equals(hwnd))
                                    {
                                        // TODO: ricezione icona nel caso la finestra aggiornata abbia un'icona diversa
                                        finestra.NomeFinestra = progName;
                                        break;
                                    }
                                }
                                servers[serverName].tableModificationsMutex.ReleaseMutex();
                                break;
                            case "OPENP":
                                /* Ricevi icona processo */
                                Bitmap bitmap = new Bitmap(64, 64);
                                bitmap.MakeTransparent(bitmap.GetPixel(1, 1));               // <-- TODO: Tentativo veloce di togliere lo sfondo nero all'icona
                                //bitmap.SetTransparencyKey(Color.White);
                                
                                Array.Clear(buffer, 0, buffer.Length);

                                // Non ci interessano: 6 byte dell'operazione, il nome del programma, il trattino, 
                                // 4 byte di dimensione icona e il trattino
                                int notBmpData = 16 + progNameLength + 1 + 4 + 1;
                                int bmpLength = BitConverter.ToInt32(msg, notBmpData - 5);

                                /* Legge i successivi bmpLength bytes e li copia nel buffer bmpData */
                                byte[] bmpData = new byte[bmpLength];

                                // Estrai dal messaggio ricevuto in bmpData solo i dati dell'icona
                                // partendo dal source offset "notBmpData"
                                Array.Copy(msg, notBmpData, bmpData, 0, bmpLength);

                                /* Crea la bitmap a partire dal byte array */
                                bitmap = CopyDataToBitmap(bmpData);
                                /* Il bitmap è salvato in memoria sottosopra, va raddrizzato */
                                bitmap.RotateFlip(RotateFlipType.RotateNoneFlipY);

                                BitmapImage bmpImage;
                                using (var b = new Bitmap(bitmap.Width, bitmap.Height))
                                {
                                    b.SetResolution(bitmap.HorizontalResolution, bitmap.VerticalResolution);

                                    using (var g = Graphics.FromImage(b))
                                    {
                                        // Clears the entire drawing surface and fills it with the specified background color
                                        g.Clear(Color.White);
                                        // Draws the specified image using its original physical size at the location specified by a coordinate pair
                                        g.DrawImageUnscaled(bitmap, 0, 0, bitmap.Width, bitmap.Height);
                                    }

                                    // Now save b like you normally would
                                    using (MemoryStream stream = new MemoryStream())
                                    {
                                        b.Save(stream, ImageFormat.Bmp);
                                        stream.Position = 0;
                                        bmpImage = new BitmapImage();
                                        bmpImage.BeginInit();
                                        // According to MSDN, "The default OnDemand cache option retains access to the stream until the image is needed."
                                        // Force the bitmap to load right now so we can dispose the stream.
                                        bmpImage.CacheOption = BitmapCacheOption.OnLoad;
                                        bmpImage.StreamSource = stream;
                                        bmpImage.EndInit();
                                        bmpImage.Freeze();                                        
                                    }

                                    servers[serverName].tableModificationsMutex.WaitOne();
                                    addItemToListView(hwnd, serverName, progName, bmpImage);
                                    servers[serverName].tableModificationsMutex.ReleaseMutex();
                                }

                                break;
                        }
                    }
                    else
                        break;
                    
                }
            }
            catch (IOException)
            {
                
            }
            catch (InvalidOperationException e)
            {
                System.Windows.MessageBox.Show(e.ToString());
            }
            catch (Exception exc)
            {
                System.Windows.MessageBox.Show("in manageNotifications(): " + exc.ToString());
                return;
            }
        }

        public Bitmap CopyDataToBitmap(byte[] data)
        {
            // Here create the Bitmap to the know height, width and format
            Bitmap bmp = new Bitmap(256, 256, PixelFormat.Format32bppRgb);

            // Create a BitmapData and Lock all pixels to be written 
            BitmapData bmpData = bmp.LockBits(
                                 new System.Drawing.Rectangle(0, 0, bmp.Width, bmp.Height),
                                 ImageLockMode.WriteOnly, bmp.PixelFormat);

            // Copy the data from the byte array into BitmapData.Scan0
            Marshal.Copy(data, 0, bmpData.Scan0, data.Length);

            // Unlock the pixels
            bmp.UnlockBits(bmpData);

            //Return the bitmap 
            return bmp;
        }

        private void disconnettiDalServer()
        {
            NetworkStream serverStream = null;
            byte[] buffer = new byte[8];
            TcpClient server = null;
            // definisci il server da disconnettere. 
            // Le strutture dati del server connesso ora sono per forza lì, solo disconnettiDalServer le può rimuovere.
            // CurrentConnectedServer può essere però cambiato, quindi lo fissiamo in modo che ci si riferisca proprio a quello.
            string disconnectingServer = currentConnectedServer;

            try
            {
                server = servers[disconnectingServer].server;
                serverStream = server.GetStream();

                // Prepara messaggio da inviare
                StringBuilder sb = new StringBuilder();
                sb.Append("--CLOSE-");                
                buffer = Encoding.ASCII.GetBytes(sb.ToString());

                // Invia richiesta chiusura
                serverStream.Write(buffer, 0, 8);
                                                
                // Aspetto che il thread manageNotifications() finisca
                servers[disconnectingServer].notificationsThread.Join();

                // Aspetto che il thread manageStatistics() finisca
                servers[disconnectingServer].statisticThread.Join();

                // Ora è possibile disabilitare e chiudere il socket.
                // La chiamata a Close() sblocca il thread eventualmente bloccato sulla Read() in attesa di dati.
                server.Close();
                
                // Aggiorna bottoni
                buttonDisconnetti.Visibility = Visibility.Hidden;
                buttonConnetti.Visibility = Visibility.Visible;
                buttonInvia.IsEnabled = false;                
                buttonCattura.IsEnabled = false;                
                
                // Svuota listView
                listView1.ItemsSource = null;

                // Rimuovi server da serverListBox
                serversListBox.Items.Remove(disconnectingServer);
                if (serversListBox.Items.Count == 0)
                {   
                    // non ci sono più server connessi
                    serversListBox.Items.Add("Nessun server connesso");
                    indirizzoServerConnesso.Content = "Nessun server connesso";
                }
                else
                {
                    // Selezioniamo il primo server della lista
                    serversListBox.SelectedIndex = 0;
                }

                // Rimuovi disconnectingServer da servers
                servers.Remove(disconnectingServer);                

                // Ripristina cattura comando
                textBoxComando.Text = "";
                buttonCattura.Visibility = Visibility.Visible;
                buttonAnnullaCattura.Visibility = Visibility.Hidden;
                comandoDaInviare.Clear();

            }
            catch (SocketException exc)
            {
                System.Windows.MessageBox.Show(exc.ToString());
            }
            catch (InvalidOperationException e)
            {
                System.Windows.MessageBox.Show(e.ToString());
            }
            catch (IOException)
            {

            }
            finally{
                if(serverStream != null )
                    serverStream.Close();
                if (server != null) 
                    server.Close(); // TODO: Nessun avviene errore. Ma si può fare Close() dopo averlo già fatto? Check.
            }
                
        }

        private void buttonDisconnetti_Click(object sender, RoutedEventArgs e)
        {
            disconnettiDalServer();
        }

        private void buttonCattura_Click(object sender, RoutedEventArgs e)
        {
            buttonCattura.IsEnabled = false;
            buttonCattura.Visibility = Visibility.Hidden;
            buttonAnnullaCattura.Visibility = Visibility.Visible;

            //_hookID = SetHook(_proc);

            // Crea event handler per scrivere i tasti premuti
            this.KeyDown += new System.Windows.Input.KeyEventHandler(OnButtonKeyDown);
        }

        private void OnButtonKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
        {
            if (textBoxComando.Text.Length == 0)
            {
                textBoxComando.Text = e.Key.ToString();
                buttonInvia.IsEnabled = true;
            }
            else
            {
                if (!textBoxComando.Text.Contains(e.Key.ToString()))
                    textBoxComando.AppendText("+" + e.Key.ToString());
            }

            // Converti c# Key in Virtual-Key da inviare al server
            comandoDaInviare.Add(KeyInterop.VirtualKeyFromKey(e.Key));
        }

        private void buttonInvia_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                byte[] messaggio;

                // Serializza messaggio da inviare
                System.Text.StringBuilder sb = new StringBuilder();
                foreach (int virtualKey in comandoDaInviare)
                {
                    if (sb.Length != 0)
                        sb.Append("+");
                    sb.Append(virtualKey.ToString());
                    System.Windows.MessageBox.Show(virtualKey.ToString());
                }
                sb.Append("\0");
                messaggio = Encoding.ASCII.GetBytes(sb.ToString());

                /* Prepara l'invio del messaggio */
                NetworkStream serverStream = null;
                
                TcpClient server = null;
                server = servers[currentConnectedServer].server;
                serverStream = server.GetStream();                
                
                // Invia messaggio
                serverStream.Write(messaggio, 0, messaggio.Length);
                                
                // Aggiorna bottoni e textBox
                textBoxComando.Text = "";
                buttonInvia.IsEnabled = false;
                buttonCattura.IsEnabled = true;
                buttonCattura.Visibility = Visibility.Visible;
                buttonAnnullaCattura.Visibility = Visibility.Hidden;

                // Svuota lista di tasti premuti da inviare
                comandoDaInviare.Clear();

                // Rimuovi event handler per non scrivere più i bottoni premuti nel textBox
                this.KeyDown -= new System.Windows.Input.KeyEventHandler(OnButtonKeyDown);
                
            }
            catch (Exception)
            {
                
            }
        }

        private void buttonAnnullaCattura_Click(object sender, RoutedEventArgs e)
        {
            // Svuota la lista di keystroke da inviare
            comandoDaInviare.Clear();

            //UnhookWindowsHookEx(_hookID);

            // Aggiorna bottoni e textBox
            textBoxComando.Text = "";
            buttonInvia.IsEnabled = false;
            buttonCattura.IsEnabled = true;
            buttonCattura.Visibility = Visibility.Visible;
            buttonAnnullaCattura.Visibility = Visibility.Hidden;
        }

        private void serversListBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            try
            {
                string selectedServer = ((sender as System.Windows.Controls.ListBox).SelectedItem as string);
                if (selectedServer != null && !selectedServer.Equals("Nessun server connesso"))
                {
                    currentConnectedServer = selectedServer;
                    listView1.ItemsSource = servers[currentConnectedServer].table.Finestre;

                    // Aggiorna interfaccia
                    buttonDisconnetti.Visibility = Visibility.Visible;
                    buttonCattura.IsEnabled = true; // per abilitare la cattura dei comandi
                    textBoxIpAddress.Text = "";     // per connessione a un nuovo server
                    indirizzoServerConnesso.Content = selectedServer;
                }
            }
            catch (InvalidOperationException ex)
            {
                System.Windows.MessageBox.Show(ex.ToString());
            }

        }        

        private const int WH_KEYBOARD_LL = 13;
        private const int WM_KEYDOWN = 0x0100;
        private static LowLevelKeyboardProc _proc = HookCallback;
        private static IntPtr _hookID = IntPtr.Zero;

        private static IntPtr SetHook(LowLevelKeyboardProc proc)
        {
            using (Process curProcess = Process.GetCurrentProcess())
            using (ProcessModule curModule = curProcess.MainModule)
            {
                return SetWindowsHookEx(WH_KEYBOARD_LL, proc,
                    GetModuleHandle(curModule.ModuleName), 0);
            }
        }

        private delegate IntPtr LowLevelKeyboardProc(int nCode, IntPtr wParam, IntPtr lParam);

        private static IntPtr HookCallback(int nCode, IntPtr wParam, IntPtr lParam)
        {
            if (nCode >= 0 && wParam == (IntPtr)WM_KEYDOWN)
            {
                int vkCode = Marshal.ReadInt32(lParam);
                Console.WriteLine((Keys)vkCode);
                System.Windows.MessageBox.Show(vkCode.ToString());
            }

            return CallNextHookEx(_hookID, nCode, wParam, lParam);
        }

        [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        private static extern IntPtr SetWindowsHookEx(int idHook, LowLevelKeyboardProc lpfn, IntPtr hMod, uint dwThreadId);

        [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool UnhookWindowsHookEx(IntPtr hhk);

        [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);

        [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        private static extern IntPtr GetModuleHandle(string lpModuleName);   

    }
}
