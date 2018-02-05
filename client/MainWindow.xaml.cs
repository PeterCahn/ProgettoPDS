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

/* TODO:
 * - Distruttore (utile ad esempio per fare Mutex.Dispose())
 * - Risolvere crash quando ci si connette a più server
 */

/* DA CHIARIRE:
 * - Quando chiudo una finestra, se questa ha una percentuale != 0, siccome la sua riga viene eliminata dalla listView le percentuali non raggiungono più il 100%:
 *   siccome c'è scritto di mostrare la percentuale di tempo in cui una finstra è stata in focus dal momento della connessione, a me questo comportamento anche se
 *   strano sembra corretto. Boh, vedere un po' di chiarire.
 * - Eccessiva lentezza nel ricevere e aggiornare la lista delle finestre aperte sul server. Possibile riduzione dimensione icona inviata lato server.
 */

namespace WpfApplication1
{
    public partial class MainWindow : Window
    {
        private byte[] buffer = new byte[1024];
        private Thread statisticsThread;
        private Thread notificationsThread;
        private List<int> comandoDaInviare = new List<int>();
        private Server currentConnectedServer;
        private Dictionary<Server, MyTable> tablesMap = new Dictionary<Server, MyTable>();
        private Dictionary<Server, Thread> notificationsThreadsList = new Dictionary<Server, Thread>();
        private Dictionary<Server, Thread> statisticsThreadsList = new Dictionary<Server, Thread>();
        private Dictionary<Server, Socket> socketsList = new Dictionary<Server, Socket>();
        
        private Dictionary<Server, ManualResetEvent> pauseThreadsList = new Dictionary<Server, ManualResetEvent>();
        //private Dictionary<Server, ManualResetEvent> pauseNotificationsThreadsList = new Dictionary<Server, ManualResetEvent>();

        private ManualResetEvent mre = new ManualResetEvent(false);

        // Mutex necessario alla gestione delle modifiche nella listView1
        private static Mutex listView1Mutex = new Mutex();

        public MainWindow()
        {
            InitializeComponent();

            // Inizialmente imposta bottoni come inutilizzabili senza connessione
            buttonDisconnetti.Visibility = Visibility.Hidden;
            buttonAnnullaCattura.Visibility = Visibility.Hidden;
            buttonInvia.IsEnabled = false;
            buttonCattura.IsEnabled = false;

        }

        private void buttonConnetti_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                // Aggiorna stato
                textBoxStato.AppendText("\nSTATO: In connessione...");

                // Crea SocketPermission per richiedere l'autorizzazione alla creazione del socket
                SocketPermission permission = new SocketPermission(NetworkAccess.Connect, TransportType.Tcp, textBoxIpAddress.Text, SocketPermission.AllPorts);

                // Accertati di aver ricevuto il permesso, altrimenti lancia eccezione
                permission.Demand();

                /* Interrogazione DNS, crea IPHostEntry contenente vettore di IPAddress di risultato          
                IPHostEntry ipHost = Dns.GetHostEntry(textIpAddress.Text); */

                // Ricava IPAddress da risultati interrogazione DNS
                string[] fields = textBoxIpAddress.Text.Split(':');
                IPAddress ipAddr = IPAddress.Parse(fields[0]);
                Int32 port = Int32.Parse(fields[1]);

                // Crea endpoint a cui connettersi
                IPEndPoint ipEndPoint = new IPEndPoint(ipAddr, port);

                // Crea socket
                Socket sock = new Socket(ipAddr.AddressFamily, SocketType.Stream, ProtocolType.Tcp);

                // Connettiti
                sock.Connect(ipEndPoint);

                // Aggiorna stato
                textBoxStato.AppendText("\nSTATO: Connesso a " + sock.RemoteEndPoint.ToString() + ".");
                textBoxStato.ScrollToEnd();

                // Aggiorna bottoni
                buttonDisconnetti.Visibility = Visibility.Visible;
                buttonConnetti.Visibility = Visibility.Hidden;
                buttonCattura.IsEnabled = true;
                textBoxIpAddress.IsEnabled = false;

                // Permetti di connettere un altro server
                buttonAltroServer.Visibility = Visibility.Visible;

                // Crea tavola per il nuovo server
                Server addedServer = new Server { Name = "IP " + fields[0] + " - Porta " + fields[1], Address = ipAddr };
                tablesMap.Add(addedServer, new MyTable());

                // Mostra la nuova tavola
                listView1.ItemsSource = tablesMap[addedServer].rowsList.DefaultView;

                // Salva il socket relativo al nuovo server
                socketsList.Add(addedServer, sock);

                // Aggiungi il nuovo server alla lista di server
                int index = serversListComboBox.Items.Add(addedServer);
                serversListComboBox.SelectedIndex = index;
                currentConnectedServer = serversListComboBox.Items[index] as Server;

                // Avvia thread per notifiche
                notificationsThread = new Thread(() => manageNotifications(addedServer));      // lambda perchè è necessario anche passare il parametro
                notificationsThread.IsBackground = true;
                notificationsThread.Name = "notif_thread_" + addedServer.ToString();
                notificationsThread.Start();
                notificationsThreadsList.Add(addedServer, notificationsThread);
                /* Aggiungi ManualResetEvent per il server aggiunto */
                //pauseNotificationsThreadsList.Add(addedServer, new ManualResetEvent(false));

                // Avvia thread per statistiche live
                statisticsThread = new Thread(() => manageStatistics(addedServer));
                statisticsThread.IsBackground = true;
                statisticsThread.Name = "stats_thread_"+addedServer.ToString();
                statisticsThread.Start();
                statisticsThreadsList.Add(addedServer, statisticsThread);
                /* Aggiungi ManualResetEvent per il server aggiunto */
                //pauseStatisticsThreadsList.Add(addedServer, new ManualResetEvent(false));

                pauseThreadsList.Add(addedServer, new ManualResetEvent(false));                

            }
            catch (Exception exc)
            {
                textBoxStato.AppendText("\nECCEZIONE: " + exc.ToString());
                textBoxStato.ScrollToEnd();
            }
        }

        void addItemToListView(Server server, string nomeProgramma, BitmapImage bmp)
        {
            listView1.Dispatcher.Invoke(delegate
            {            
                // Aggiungi il nuovo elemento alla tabella
                DataRow newRow = tablesMap[server].rowsList.NewRow();
                newRow["Nome applicazione"] = nomeProgramma;
                newRow["Stato finestra"] = "Background";
                newRow["Tempo in focus (%)"] = 0;
                newRow["Tempo in focus"] = 0;
                newRow["Icona"] = bmp;
                tablesMap[server].rowsList.Rows.Add(newRow);
            });

            //var imageConverter = new ImageConverter();
            //newRow["Icona"] = imageConverter.ConvertTo(bmp, System.Type.GetType("System.Byte[]"));
        }

        /* Viene eseguito in un thread a parte.
         * Si occupa della gestione delle statistiche, aggiornando le percentuali di Focus ogni 500ms.
         * In questo modo abbiamo statistiche "live"
         */
        private void manageStatistics(Server server)
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
                    
                    //Thread.Sleep(500);
                    bool isSignaled = pauseThreadsList[server].WaitOne(500);
                    if (isSignaled) break;

                    // Delegato necessario per poter aggiornare la listView, dato che operazioni come Refresh() possono essere chiamate
                    // solo dal thread proprietario, che è quello principale e non quello che esegue manageStatistics()
                    listView1.Dispatcher.Invoke(delegate
                        {
                            foreach (DataRow item in tablesMap[server].rowsList.Rows)
                            {

                                if (item["Stato finestra"].Equals("Focus"))
                                {
                                    item["Tempo in focus"] = (DateTime.Now - lastUpdate).TotalMilliseconds + (double)item["Tempo in focus"];
                                    lastUpdate = DateTime.Now;
                                }

                                // Calcola la percentuale
                                item["Tempo in focus (%)"] = ((double)item["Tempo in focus"] / (DateTime.Now - connectionTime).TotalMilliseconds * 100).ToString("n2");
                            }

                            listView1.Items.Refresh();
                        });                                        
                }
            }
            catch (ThreadInterruptedException exception)
            {
                // TODO: c'è qualcosa da fare?
                // TODO: check se il thread muore davvero
                listView1.Dispatcher.Invoke(delegate
                {
                    textBoxStato.AppendText("Thread che riceve aggiornamenti dal server interrotto.\n");
                    textBoxStato.ScrollToEnd();
                    return;
                });                
            }
        }

        /* Viene eseguito in un thread a parte.
         * Si occupa della ricezione e gestione delle notifiche.
         */
        private void manageNotifications(Server server)
        {
            byte[] buf = new byte[1024];
            StringBuilder completeMessage = new StringBuilder();
            NetworkStream networkStream = new NetworkStream(socketsList[server]);

            // Vecchia condizione: !((sock.Poll(1000, SelectMode.SelectRead) && (sock.Available == 0)) || !sock.Connected
            // Poll() ritorna true se la connessione è chiusa, resettata, terminata o in attesa (non attiva), oppure se è attiva e ci sono dati da leggere
            // Available() ritorna il numero di dati da leggere
            // Se Available() è 0 e Poll() ritorna true, la connessione è chiusa
            
            try
            {
                while (networkStream.CanRead && socketsList[server].Connected)
                {
                    bool isSignaled = pauseThreadsList[server].WaitOne(0);
                    if (isSignaled) break;

                    
                    int i = 0;
                    int progNameLength = 0;
                    string progName = null;
                    string operation = null;
                    StringBuilder sb = new StringBuilder();
                    Array.Clear(buf, 0, buf.Length);


                    /* Leggi e salva il tipo di operazione */
                    char c;
                    sb.Append((char)networkStream.ReadByte());
                    sb.Append((char)networkStream.ReadByte());
                    do
                    {
                        c = (char)networkStream.ReadByte();
                        sb.Append(c);

                    } while (c != '-');
                    operation = sb.ToString();
                    sb.Clear();

                    /* Leggi e salva lunghezza nome programma */
                    i = 0;
                    do
                    {
                        c = (char)networkStream.ReadByte();
                        buf[i++] = (byte)c;
                    }
                    while (networkStream.DataAvailable && c != '-');
                    progNameLength = Int32.Parse(Encoding.ASCII.GetString(buf, 0, i - 1));

                    /* Leggi e salva nome programma */
                    byte[] buffer = new byte[progNameLength];
                    networkStream.Read(buffer, 0, progNameLength);
                    progName = Encoding.ASCII.GetString(buffer, 0, progNameLength);

                    /* Possibili valori ricevuti:
                        * --FOCUS-<lunghezza_nome>-<nome_nuova_app_focus>
                        * --CLOSE-<lunghezza_nome>-<nome_app_chiusa>
                        * --OPENP-<lunghezza_nome>-<nome_nuova_app_aperta>-<dimensione_bitmap>-<bitmap>
                        */

                    switch (operation)
                    {
                        case "--FOCUS-":
                            // Cambia programma col focus
                            foreach (DataRow item in tablesMap[server].rowsList.Rows)
                            {
                                if (item["Nome applicazione"].Equals(progName))
                                    item["Stato finestra"] = "Focus";
                                else if (item["Stato finestra"].Equals("Focus"))
                                    item["Stato finestra"] = "Background";
                            }
                            break;
                        case "--CLOSE-":
                            // Rimuovi programma dalla listView
                            foreach (DataRow item in tablesMap[server].rowsList.Rows)
                            {
                                if (item["Nome applicazione"].Equals(progName))
                                {
                                    tablesMap[server].rowsList.Rows.Remove(item);
                                    break;
                                }
                            }
                            break;
                        case "--OPENP-":
                            /* Ricevi icona processo */
                            Bitmap bitmap = new Bitmap(64, 64);
                            bitmap.MakeTransparent();               // <-- TODO: Tentativo veloce di togliere lo sfondo nero all'icona
                            Array.Clear(buf, 0, buf.Length);

                            /* Leggi e salva la lunghezza dell'icona leggendo i successivi byte fino a '-' (escludendo il primo) */
                            i = 0;
                            c = (char)networkStream.ReadByte(); // leggi il primo trattino
                            do
                            {
                                c = (char)networkStream.ReadByte();
                                buf[i++] = (byte)c;

                            } while (networkStream.DataAvailable && c != '-');
                            int bmpLength = Int32.Parse(Encoding.ASCII.GetString(buf, 0, i - 1));


                            /* Legge i successivi bmpLength bytes e li copia nel buffer bmpData */
                            byte[] bmpData = new byte[bmpLength];
                            int received = 0;
                            while (received < bmpLength)
                            {
                                received += networkStream.Read(bmpData, received, bmpLength - received);
                            }

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

                            addItemToListView(server, progName, bmpImage);
                            break;
                        
                    }
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
                listView1.Dispatcher.Invoke(delegate
                {
                    textBoxStato.AppendText("\nECCEZIONE: " + ioe.ToString());
                    textBoxStato.ScrollToEnd();
                });
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

        private void buttonDisconentti_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                /* Set sul ManualResetEvent per uscire dal while loop e poter terminare il thread */
                pauseThreadsList[currentConnectedServer].Set();
                textBoxStato.AppendText("\nSTATO: Disconnessione da "+currentConnectedServer+"...");
                //Thread.Sleep(1000);

                // Join dei thread
                notificationsThreadsList[currentConnectedServer].Join();
                statisticsThreadsList[currentConnectedServer].Join();

                // Resetta il ManualResetEvent al valore iniziale
                pauseThreadsList[currentConnectedServer].Reset();

                // Disabilita e chiudi socket
                socketsList[currentConnectedServer].Shutdown(SocketShutdown.Both);
                socketsList[currentConnectedServer].Close();

                // Aggiorna bottoni
                buttonDisconnetti.Visibility = Visibility.Hidden;
                buttonConnetti.Visibility = Visibility.Visible;
                buttonInvia.IsEnabled = false;
                textBoxIpAddress.IsEnabled = true;
                buttonCattura.IsEnabled = false;
                buttonAltroServer.Visibility = Visibility.Hidden;

                // Aggiorna stato
                textBoxStato.AppendText("\nSTATO: Disconnesso da " + currentConnectedServer + "...");
                textBoxStato.ScrollToEnd();

                // Svuota listView
                if (listView1Mutex.WaitOne(500))
                {
                    listView1.ItemsSource = null;
                    listView1Mutex.ReleaseMutex();
                }

                // Rimuovi server da serverListComboBox
                serversListComboBox.Items.Remove(currentConnectedServer);

                // Rimuovi notificationsThread da notificationsThreadsList
                notificationsThreadsList.Remove(currentConnectedServer);
                
                // Rimuovi statisticsThread da statisticsThreadsList
                statisticsThreadsList.Remove(currentConnectedServer);

                // Rimuovi il ManualResetEvent
                pauseThreadsList.Remove(currentConnectedServer);

                // Rimuovi il socket del server
                socketsList.Remove(currentConnectedServer);

                // Rimuovi server da tablesMap
                tablesMap.Remove(currentConnectedServer);                                
                
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

        private void buttonCattura_Click(object sender, RoutedEventArgs e)
        {
            buttonCattura.IsEnabled = false;
            buttonCattura.Visibility = Visibility.Hidden;
            buttonAnnullaCattura.Visibility = Visibility.Visible;

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
                }
                sb.Append("\0");
                messaggio = Encoding.ASCII.GetBytes(sb.ToString());

                // Invia messaggio
                int NumDiBytesInviati = socketsList[currentConnectedServer].Send(messaggio);

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
                textBoxStato.AppendText("\nECCEZIONE: " + exc.ToString());
                textBoxStato.ScrollToEnd();
            }
        }

        private void buttonAnnullaCattura_Click(object sender, RoutedEventArgs e)
        {
            // Svuota la lista di keystroke da inviare
            comandoDaInviare.Clear();

            // Aggiorna bottoni e textBox
            textBoxComando.Text = "";
            buttonInvia.IsEnabled = false;
            buttonCattura.IsEnabled = true;
            buttonCattura.Visibility = Visibility.Visible;
            buttonAnnullaCattura.Visibility = Visibility.Hidden;
        }

        private void serversListComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            Server selectedServer = ((sender as System.Windows.Controls.ComboBox).SelectedItem as Server);
            if (selectedServer != null)
            {
                currentConnectedServer = selectedServer;
                listView1.ItemsSource = tablesMap[currentConnectedServer].rowsList.DefaultView;

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
            if (listView1Mutex.WaitOne(1000))
            {
                listView1.ItemsSource = null;
                listView1Mutex.ReleaseMutex();
            }

            serversListComboBox.SelectedValue = 0;
        }
    }
}
