﻿using System;
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

/* TODO:
 * - Distruttore (utile ad esempio per fare Mutex.Dispose())
 * - Main thread si sblocca e mostra che si è disconnesso solo quando si clicca fuori dalla finestra (dopo aver premuto su "Disconnetti")
 */

/* DA CHIARIRE:
 * - Quando chiudo una finestra, se questa ha una percentuale != 0, siccome la sua riga viene eliminata dalla listView le percentuali non raggiungono più il 100%:
 *   siccome c'è scritto di mostrare la percentuale di tempo in cui una finstra è stata in focus dal momento della connessione, a me questo comportamento anche se
 *   strano sembra corretto. Boh, vedere un po' di chiarire. 
 */

namespace WpfApplication1
{
    public partial class MainWindow : Window
    {
        private byte[] buffer = new byte[1024];
        private Thread statisticsThread;
        private Thread notificationsThread;
        private List<int> comandoDaInviare = new List<int>();
        private string currentConnectedServer;
        private string _textBoxString = "";
        public string TextBoxString {
            get { return _textBoxString; }
            set {
                if (value != _textBoxString)
                {
                    _textBoxString = value;
                    OnPropertyChanged("TextBoxString");
                }
            } } // stringa su cui fare il bind per cambiare il testo della TextBox
        private Dictionary<string, ServerInfo> servers = new Dictionary<string, ServerInfo>();
        
        /* Mutex necessario alla gestione delle modifiche nella listView1 perchè i thread statistics and notifications
         * accedono alla stessa tablesMap[currentConnectedServer]
         */
        private static Mutex tablesMapsEntryMutex = new Mutex();
        /* Mutex necessario affinché non venga chiamata una Invoke mentre il main thread è in attesa sulla Join 
         * creando una dipendendza ciclica.
         */
        private static Mutex invokeMutex = new Mutex();

        public event PropertyChangedEventHandler PropertyChanged;

        public MainWindow()
        {
            InitializeComponent();

            // Inizialmente imposta bottoni come inutilizzabili senza connessione
            buttonDisconnetti.Visibility = Visibility.Hidden;
            buttonAnnullaCattura.Visibility = Visibility.Hidden;
            buttonInvia.IsEnabled = false;
            buttonCattura.IsEnabled = false;

            TextBoxString = "STATO: Disconnesso.";
        }

        // Create the OnPropertyChanged method to raise the event
        protected void OnPropertyChanged(string text)
        {
            PropertyChangedEventHandler handler = PropertyChanged;
            if (handler != null)
            {
                handler(this, new PropertyChangedEventArgs(text));
            }
        }

        private void buttonConnetti_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                // Aggiorna stato
                textBoxStato.AppendText("STATO: In connessione...");
                
                // Ricava IPAddress da risultati interrogazione DNS
                string[] fields = textBoxIpAddress.Text.Split(':');
                Int32 port = Int32.Parse(fields[1]);
                TcpClient server = new TcpClient(fields[0], port);                

                ServerInfo si = new ServerInfo();
                si.server = server;

                // string serverEndpoint = ((IPEndPoint)server.Client.RemoteEndPoint).Address.ToString();
                string serverName = fields[0] + ":" + fields[1];

                // Aggiorna stato
                textBoxStato.AppendText("STATO: Connesso a " + fields[0] + ":" + fields[1]);
                textBoxStato.ScrollToEnd();

                // Aggiorna bottoni
                buttonDisconnetti.Visibility = Visibility.Visible;
                buttonConnetti.Visibility = Visibility.Hidden;
                buttonCattura.IsEnabled = true;
                textBoxIpAddress.IsEnabled = false;

                // Permetti di connettere un altro server
                buttonAltroServer.Visibility = Visibility.Visible;

                // Aggiungi MyTable vuota in ServerInfo
                si.table = new MyTable();

                // Mostra la nuova tavola
                listView1.ItemsSource = si.table.rowsList.DefaultView;

                // Avvia thread per notifiche
                notificationsThread = new Thread(() => manageNotifications(serverName));      // lambda perchè è necessario anche passare il parametro
                notificationsThread.IsBackground = true;
                notificationsThread.Name = "notif_thread_" + serverName;
                notificationsThread.Start();
                si.notificationsTread = notificationsThread;

                // Avvia thread per statistiche live
                statisticsThread = new Thread(() => manageStatistics(serverName));
                statisticsThread.IsBackground = true;
                statisticsThread.Name = "stats_thread_"+ serverName;
                statisticsThread.Start();
                si.statisticTread = statisticsThread;

                // ManualResetEvent settato a false perché il thread si blocchi quando chiama WaitOne
                si.disconnectionEvent = new ManualResetEvent(false);

                // Aggiungi il ServerInfo alla lista dei "servers"
                servers.Add(serverName, si);

                // Aggiungi il nuovo server alla lista di server nella lista combo box
                int index = serversListComboBox.Items.Add(serverName);
                serversListComboBox.SelectedIndex = index;
                currentConnectedServer = serversListComboBox.Items[index] as string;
            }
            catch (SocketException)
            {
                textBoxStato.AppendText("ERRORE: Problema riscontrato nella connessione al server. Riprovare.\n");
                return;
            }
            catch (IOException ioe)
            {
                textBoxStato.AppendText("ECCEZIONE: " + ioe.ToString() + "\n");
                return;
            }
            catch (Exception exc)
            {
                textBoxStato.AppendText("ECCEZIONE: " + exc.ToString() + "\n");
                textBoxStato.ScrollToEnd();
            }
        }

        void addItemToListView(string server, string nomeProgramma, BitmapImage bmp)
        {
            // Aggiungi il nuovo elemento alla tabella
            DataRow newRow = servers[server].table.rowsList.NewRow();
            newRow["Nome applicazione"] = nomeProgramma;
            newRow["Stato finestra"] = "Background";
            newRow["Tempo in focus (%)"] = 0;
            newRow["Tempo in focus"] = 0;
            newRow["Icona"] = bmp;
            servers[server].table.rowsList.Rows.Add(newRow);            
            
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

                // Sleep() necessario per evitare divisione per 0 alla prima iterazione e mostrare NaN per il primo mezzo secondo nelle statistiche
                //      Piero: senza sleep (dopo aver messo l'icona), non viene mostrato nessun NaN
                // Thread.Sleep(1);
                while (true)
                {
                    // il MutualResetEvent disconnectionEvent è usato anche per far attendere il thread nell'aggiornare le statistiche
                    bool isSignaled = servers[serverName].disconnectionEvent.WaitOne(500);
                    if (isSignaled) break;

                    tablesMapsEntryMutex.WaitOne();
                    foreach (DataRow item in servers[serverName].table.rowsList.Rows)
                    {
                        if (item["Stato finestra"].Equals("Focus"))
                        {
                            item["Tempo in focus"] = (DateTime.Now - lastUpdate).TotalMilliseconds + (double)item["Tempo in focus"];
                            lastUpdate = DateTime.Now;
                        }

                        // Calcola la percentuale
                        item["Tempo in focus (%)"] = ((double)item["Tempo in focus"] / (DateTime.Now - connectionTime).TotalMilliseconds * 100).ToString("n2");
                    }
                    
                    tablesMapsEntryMutex.ReleaseMutex();

                    // Delegato necessario per poter aggiornare la listView, dato che operazioni come Refresh() possono essere chiamate
                    // solo dal thread proprietario, che è quello principale e non quello che esegue manageStatistics()
                    Dispatcher.BeginInvoke((Action)(() =>
                    {
                        listView1.Items.Refresh();
                    }));

                }
            }
            catch (ThreadInterruptedException exception)
            {
                // TODO: c'è qualcosa da fare?
                // TODO: check se il thread muore davvero
                Dispatcher.Invoke(delegate
                {
                    textBoxStato.AppendText("Thread che riceve aggiornamenti dal server interrotto.\n");
                    textBoxStato.ScrollToEnd();
                    return;
                });
            }
            catch (Exception exc)
            {
                System.Windows.MessageBox.Show("manageStatistics(): " + exc.ToString());
            }
        }

        /* Viene eseguito in un thread a parte.
         * Si occupa della ricezione e gestione delle notifiche.
         */
        private void manageNotifications(string serverName)
        {
            NetworkStream serverStream;
            int bytesRead = 0;
            int bytesReceived = 0;
            byte[] buffer = new byte[7];

            TcpClient server = servers[serverName].server;
            serverStream = server.GetStream();

            string operation = null;
            string progName = null;                        

            // Vecchia condizione: !((sock.Poll(1000, SelectMode.SelectRead) && (sock.Available == 0)) || !sock.Connected
            // Poll() ritorna true se la connessione è chiusa, resettata, terminata o in attesa (non attiva), oppure se è attiva e ci sono dati da leggere
            // Available() ritorna il numero di dati da leggere
            // Se Available() è 0 e Poll() ritorna true, la connessione è chiusa

            try
            {
                //while (networkStream.CanRead && servers[server].socket.Connected)
                //while(!((servers[serverName].socket.Poll(1000, SelectMode.SelectRead) && (servers[serverName].socket.Available == 0)) || !servers[serverName].socket.Connected))
                while(serverStream.CanRead && server.Connected)
                {
                    bool isSignaled = servers[serverName].disconnectionEvent.WaitOne(1);
                    if (isSignaled) break;

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
                    
                    // Estrai lunghezza nome programma => offset 6 (offset 5 è il '-' che precede)
                    int progNameLength = BitConverter.ToInt32(msg, 6);
                    // Leggi nome del programma => da offset 11 (6 di operazione + 5 di dimensione (incluso 1 di trattino))
                    byte[] pN = new byte[progNameLength];
                    Array.Copy(msg, 5 + 6, pN, 0, progNameLength);
                    progName = Encoding.ASCII.GetString(pN);

                    /* Possibili valori ricevuti:
                        * --<4 bytes di dimensione messaggio>-FOCUS-<4B per dimensione nome programma>-<nome_nuova_app_focus>
                        * --<4 bytes di dimensione messaggio>-CLOSE-<4B per dimensione nome programma>-<nome_app_chiusa>
                        * --<4 bytes di dimensione messaggio>-OPENP-<4B per dimensione nome programma>-<nome_nuova_app_aperta>-<4B di dimensione icona>-<bitmap>
                        */

                    switch (operation)
                    {
                        case "FOCUS":
                            // Cambia programma col focus
                            tablesMapsEntryMutex.WaitOne();
                            foreach (DataRow item in servers[serverName].table.rowsList.Rows)
                            {
                                if (item["Nome applicazione"].Equals(progName))
                                    item["Stato finestra"] = "Focus";
                                else if (item["Stato finestra"].Equals("Focus"))
                                    item["Stato finestra"] = "Background";
                            }
                            tablesMapsEntryMutex.ReleaseMutex();

                            break;
                        case "CLOSE":
                            // Rimuovi programma dalla listView
                            tablesMapsEntryMutex.WaitOne();
                            foreach (DataRow item in servers[serverName].table.rowsList.Rows)
                            {
                                if (item["Nome applicazione"].Equals(progName))
                                {
                                    servers[serverName].table.rowsList.Rows.Remove(item);
                                    break;
                                }
                            }
                            tablesMapsEntryMutex.ReleaseMutex();

                            break;
                        case "OPENP":
                            /* Ricevi icona processo */
                            Bitmap bitmap = new Bitmap(64, 64);
                            bitmap.MakeTransparent();               // <-- TODO: Tentativo veloce di togliere lo sfondo nero all'icona
                            Array.Clear(buffer, 0, buffer.Length);

                            // Non ci interessano: 6 byte dell'operazione, il nome del programma, il trattino, 
                            // 4 byte di dimensione icona e il trattino
                            int notBmpData = 11 + progName.Length + 1 + 4 + 1;                            
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
                            using (MemoryStream stream = new MemoryStream())
                            {
                                bitmap.Save(stream, ImageFormat.Bmp);
                                stream.Position = 0;
                                BitmapImage result = new BitmapImage();
                                result.BeginInit();
                                // According to MSDN, "The default OnDemand cache option retains access to the stream until the image is needed."
                                // Force the bitmap to load right now so we can dispose the stream.
                                result.CacheOption = BitmapCacheOption.OnLoad;
                                result.StreamSource = stream;
                                result.EndInit();
                                result.Freeze();
                                bmpImage = result;
                            }

                            tablesMapsEntryMutex.WaitOne();
                            addItemToListView(serverName, progName, bmpImage);
                            tablesMapsEntryMutex.ReleaseMutex();

                            break;
                    }

                    bytesRead = 0;
                    bytesReceived = 0;
                }
            }
            catch (ThreadInterruptedException exception)
            {
                // TODO: c'è qualcosa da fare?
                // TODO: check che il thread muoia davvero
                listView1.Dispatcher.Invoke(delegate
                {
                    textBoxStato.AppendText("Thread che riceve aggiornamenti dal server interrotto.\n");
                    textBoxStato.ScrollToEnd();
                });
                
                return;
            }
            catch (IOException ioe)
            {
                Dispatcher.Invoke(delegate
                {
                    textBoxStato.AppendText("ERRORE: Si è verificato un problema nella connessione al server.\n");
                    textBoxStato.ScrollToEnd();
                    //disconnettiDalServer();
                });
                
            }
            catch (Exception exc)
            {
                System.Windows.MessageBox.Show("manageNotifications(): " + exc.ToString());
                return;
            }
            finally
            {
                serverStream.Close();
            }
        }

        public Bitmap CopyDataToBitmap(byte[] data)
        {
            // Here create the Bitmap to the know height, width and format
            Bitmap bmp = new Bitmap(256, 256, System.Drawing.Imaging.PixelFormat.Format32bppRgb);

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
            try
            {
                textBoxStato.AppendText("STATO: Disconnessione da " + currentConnectedServer + " in corso...\n");

                // Set del ManualResetEvent "disconnectionEvent" per uscire dal loop in manageNotifications() e manageStatistics()
                servers[currentConnectedServer].disconnectionEvent.Set();

                // Aspetto che il thread manageStatistics() finisca
                servers[currentConnectedServer].statisticTread.Join();

                // Aspetto che il thread manageNotifications() finisca
                servers[currentConnectedServer].notificationsTread.Join();

                servers[currentConnectedServer].disconnectionEvent.Reset(); // reset del ManualResetEvent

                // Ora è possibile disabilitare e chiudere il socket
                servers[currentConnectedServer].server.Close();

                // Aggiorna bottoni
                buttonDisconnetti.Visibility = Visibility.Hidden;
                buttonConnetti.Visibility = Visibility.Visible;
                buttonInvia.IsEnabled = false;
                textBoxIpAddress.IsEnabled = true;
                buttonCattura.IsEnabled = false;
                buttonAltroServer.Visibility = Visibility.Hidden;

                // Aggiorna stato
                textBoxStato.AppendText("STATO: Disconnesso da " + currentConnectedServer + "\n");
                textBoxStato.ScrollToEnd();

                // Svuota listView
                tablesMapsEntryMutex.WaitOne();
                listView1.ItemsSource = null;
                tablesMapsEntryMutex.ReleaseMutex();
                
                // Rimuovi server da serverListComboBox
                serversListComboBox.Items.Remove(currentConnectedServer);
                
                // Rimuovi currencConnectedServer da servers
                servers.Remove(currentConnectedServer);

                // Ripristina cattura comando
                textBoxComando.Text = "";
                buttonCattura.Visibility = Visibility.Visible;
                buttonAnnullaCattura.Visibility = Visibility.Hidden;
                comandoDaInviare.Clear();

            }
            catch (Exception exc)
            {
                System.Windows.MessageBox.Show("BUTTON DISCONNETTI: " + exc.ToString());
            }
        }

        private void buttonDisconentti_Click(object sender, RoutedEventArgs e)
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
                System.Text.StringBuilder sb = new System.Text.StringBuilder();
                foreach (int virtualKey in comandoDaInviare)
                {
                    if (sb.Length != 0)
                        sb.Append("+");
                    sb.Append(virtualKey.ToString());
                    System.Windows.MessageBox.Show(virtualKey.ToString());
                }
                sb.Append("\0");
                messaggio = Encoding.ASCII.GetBytes(sb.ToString());
                
                // Invia messaggio
                //int NumDiBytesInviati = servers[currentConnectedServer].server.Send(messaggio);

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
            catch (Exception exc)
            {
                textBoxStato.AppendText("ECCEZIONE: " + exc.ToString() + "\n");
                textBoxStato.ScrollToEnd();
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

        private void serversListComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            string selectedServer = ((sender as System.Windows.Controls.ComboBox).SelectedItem as string);
            if (selectedServer != null)
            {
                currentConnectedServer = selectedServer;
                listView1.ItemsSource = servers[currentConnectedServer].table.rowsList.DefaultView;

                // Aggiorna bottoni
                buttonDisconnetti.Visibility = Visibility.Visible;
                buttonConnetti.Visibility = Visibility.Hidden;
                buttonCattura.IsEnabled = true;
                textBoxIpAddress.IsEnabled = false;

                // Permetti di connettere un altro server
                buttonAltroServer.Visibility = Visibility.Visible;
            }            
        }

        private void buttonAltroServer_Click(object sender, RoutedEventArgs e)
        {
            // Pulizia interfaccia
            buttonAnnullaCattura_Click(null, null);
            buttonDisconnetti.Visibility = Visibility.Hidden;
            buttonConnetti.Visibility = Visibility.Visible;
            buttonInvia.IsEnabled = false;
            textBoxIpAddress.IsEnabled = true;
            textBoxIpAddress.Text = "";
            buttonCattura.IsEnabled = false;
            buttonAltroServer.Visibility = Visibility.Hidden;

            // Svuota listView
            tablesMapsEntryMutex.WaitOne();
            listView1.ItemsSource = null;
            tablesMapsEntryMutex.ReleaseMutex();
            
            serversListComboBox.SelectedValue = 0;
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
