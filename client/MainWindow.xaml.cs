using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
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
using System.ComponentModel;
using System.Windows.Forms;
using System.Text.RegularExpressions;

/* TODO:
 * - Distruttore (utile ad esempio per fare Mutex.Dispose())
 * - Icona con sfondo nero 
 * - Modifica controllo numero server connessi. Crash al controllo se serversListBox.Items[0].Equals("Nessun server connesso")
 *  => Aggiungere elemento che viene mostrato solo quando non ci sono server connessi. Così il controllo è solo sulla size della lista.
 */

namespace WpfApplication1
{
    public partial class MainWindow : Window
    {
        private const int FREQUENZA_AGGIORNAMENTO_STATISTICHE = 500;
        private List<int> comandoDaInviare = new List<int>();
        private string currentConnectedServer;
        private Dictionary<string, ServerInfo> servers = new Dictionary<string, ServerInfo>();

        /* BackgroundWorker necessario per evitare che il main thread si blocchi 
         * mentre aspetta che si instauri la connessione con un nuovo server. 
         * Situazione tipo: non è possibile connettersi al server finché non scade il timeout di connessione nella TcpClient. */
        BackgroundWorker bw = new BackgroundWorker();
        string connectingIp;
        int connectingPort;

        /* Mutex necessario alla gestione delle modifiche nella listView1 perchè 
         * i thread accedono alla stessa variabile 'servers'.
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

            disabilitaERimuoviCatturaComando();

            // Aggiunge gli event handler al BackgroundWorker che gestisce il tentativo di connessione a un nuovo server
            bw.DoWork += new DoWorkEventHandler(provaConnessione);
            bw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(finalizzaConnessione);

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
                iniziaConnessione();
            }
        }

        private void buttonConnetti_Click(object sender, RoutedEventArgs e)
        {

            //connettiAlServer();   // connessione sincrona

            iniziaConnessione();    // connessione asincrona
        }

        private void iniziaConnessione()
        {
            IPEndPoint ipPort = null;
            string ipAddress = null;
            int port = -1;
            string serverName = null;            

            /* Ottieni il l'indirizzo IP e la porta a cui connettersi. */
            ipPort = parseHostPort(textBoxIpAddress.Text);
            if (ipPort == null)
            {
                System.Windows.MessageBox.Show("Formato ammesso: [0-255].[0-255].[0-255].[0.255]:[1024-65535]");
                return;
            }

            ipAddress = ipPort.Address.ToString();
            port = ipPort.Port;

            serverName = ipAddress + ":" + port;

            /* Controlla che non ci sia già serverName tra i server a cui si è connessi */
            lock (servers)
            {
                if (servers.ContainsKey(serverName))
                {
                    if (servers[serverName].isOnline)
                    {
                        System.Windows.MessageBox.Show("Già connessi al server " + serverName);
                        return;
                    }
                }
            }

            if (bw.IsBusy != true)
            {
                connectingIp = ipAddress;
                connectingPort = port;

                textBoxIpAddress.IsEnabled = false;
                buttonConnetti.IsEnabled = false;

                bw.RunWorkerAsync();
            }
        }

        private void provaConnessione(object sender, DoWorkEventArgs e)
        {
            try
            {
                e.Result = new TcpClient(connectingIp, connectingPort);
                /* ArgumentNullException: hostname is null
                 * ArgumentOutOfRangeException: port non è tra MinPort e MaxPort */
                 
            }
            catch (SocketException se)
            {
                int errorCode = se.ErrorCode;
                if (errorCode.Equals(SocketError.TimedOut))
                    System.Windows.MessageBox.Show("Tentativo di connessione al server " + connectingIp+":"+connectingPort + " scaduto.");
                else
                    System.Windows.MessageBox.Show("Connessione al server " + connectingIp + ":" + connectingPort + " fallita.");

                return; // Usciamo perché l'operazione non è andata a buon fine. Il nuovo tentativo sarà manuale.
            }
        }

        private void finalizzaConnessione(object sender, RunWorkerCompletedEventArgs e)
        {
            TcpClient s = (TcpClient) e.Result;
            if (s == null)
            {
                // Non è stato possibile connettersi, quindi ritorna
                textBoxIpAddress.IsEnabled = true;
                buttonConnetti.IsEnabled = true;
                return;
            }
            else
            {
                // Connessione riuscita, riabilita i pulsanti per connettersi a un nuovo server
                textBoxIpAddress.IsEnabled = true;
                buttonConnetti.IsEnabled = true;
            }

            string serverName = connectingIp + ":" + connectingPort;
            ServerInfo si = new ServerInfo(serverName, (TcpClient)e.Result, true);

            // Se già è presente una connessione a quel server ma era offline, rimuovi i suoi riferimenti
            // che erano già stati precedentemente invalidati, in modo da poterlo riaggiungere alla lista
            aggiungiServer(serverName, si);

            /* Lancio dei thread posticipato a quando la chiave "serverName" è effettivamente inserita
             * per evitare che i thread riferiscano ad una chiave ancora non esistente.
             * Una volta partiti tutto il necessario sarà presente in servers[serverName].
             * I riferimenti dei thread per monitorare l'uscita sono legati direttamente nel ServerInfo alla creazione dei thread stessi.
             */

            // I metodi di BackgroundWorker qui chiamati non lanciano eccezioni rilevanti.
            // RunWorkerAsync lancia InvalidOperationException se il metodo IsBusy è true, ma il thread è stato appena creato.

            // Aggiorna statistiche
            servers[serverName].statisticsBw = new BackgroundWorker();
            servers[serverName].statisticsBw.WorkerSupportsCancellation = true;
            servers[serverName].statisticsBw.DoWork += new DoWorkEventHandler(aggiornaStatistiche);
            servers[serverName].statisticsBw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(aggiornaStatisticheTerminato);
            servers[serverName].statisticsBw.RunWorkerAsync(serverName);

            // Gestisci notifiche
            servers[serverName].notificationsBw = new BackgroundWorker();
            servers[serverName].notificationsBw.WorkerSupportsCancellation = true;
            servers[serverName].notificationsBw.DoWork += new DoWorkEventHandler(riceviNotifiche);
            servers[serverName].notificationsBw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(riceviNotificheTerminato);
            servers[serverName].notificationsBw.RunWorkerAsync(serverName);

            // Aggiorna bottoni
            buttonDisconnetti.Visibility = Visibility.Visible;
            textBoxIpAddress.Text = "";

            // Mostra il nuovo elenco
            listView1.ItemsSource = si.table.Finestre;
            
            listView1.Focus(); // per togliere il focus dalla textBoxIpAddress

        }

        /* Come manageStatistics ma da eseguire in un BackgroundWorker */
        private void aggiornaStatistiche(object sender, DoWorkEventArgs e)
        {
            string serverName = (string) e.Argument;
            BackgroundWorker worker = sender as BackgroundWorker;

            while (true)
            {
                // Controlla se è stata chiamata CancelAsync() sul BackgroundThread che esegue aggiornaStatistiche
                if (worker.CancellationPending == true)
                {
                    e.Cancel = true;
                    break;
                }
                else
                {
                    /* Controlla che 'serverName' non sia stata eliminata */
                    lock (servers)
                    {
                        if (servers.ContainsKey(serverName)) // l'argomento non può essere null perché validato
                        {
                            servers[serverName].table.aggiornaStatisticheFocus();
                        }
                    }
                    Thread.Sleep(FREQUENZA_AGGIORNAMENTO_STATISTICHE);
                }
            }
        }

        /* Chiamato al termine di aggiornaStatistiche */
        private void aggiornaStatisticheTerminato(object sender, RunWorkerCompletedEventArgs e)
        {
            BackgroundWorker worker = sender as BackgroundWorker;

            if (e.Cancelled == true)
            {
                //System.Windows.MessageBox.Show("BackgroundWorker aggiornaStatistiche cancellato.");
            }

            else if (e.Error != null)
            {
                //System.Windows.MessageBox.Show("BackgroundWorker aggiornaStatistiche terminato con errori.");
            }
            else
            {
                //System.Windows.MessageBox.Show("BackgroundWorker aggiornaStatistiche terminato normalmente.");
            }

            worker.Dispose();
        }
        
        /* Come manageNotifications ma da eseguire in un BackgroundWorker */
        private void riceviNotifiche(object sender, DoWorkEventArgs e)
        {
            string serverName = (string) e.Argument;
            BackgroundWorker worker = sender as BackgroundWorker;
            TcpClient server = null;

            NetworkStream serverStream = null;
            byte[] buffer = new byte[7];
            Array.Clear(buffer, 0, 7);
            
            try
            {
                server = servers[serverName].server;
                serverStream = server.GetStream();
            }
            catch (KeyNotFoundException)
            {
                /* Eccezione scatenata se serverName non c'è più in 'servers' */
                System.Windows.MessageBox.Show("Problema inaspettato durante la ricezione delle notifiche.\nArresto ricezione notifiche per il server " + serverName + ".");
                servers[serverName].notificationsBw.CancelAsync();
                servers[serverName].statisticsBw.CancelAsync();
                
                // Chiudi la connessione con il server
                if (servers[serverName].server.Connected)
                    servers[serverName].server.Close();

                // TODO: Pulisci interfaccia in questo caso
                safePulisciInterfaccia(serverName, false);

                return;
            }
            catch(Exception)
            {
                /* Eccezione scatenata se serverName non c'è più in 'servers' */
                System.Windows.MessageBox.Show("Problema inaspettato durante la ricezione delle notifiche.\nArresto ricezione notifiche per il server " + serverName + ".");
                servers[serverName].notificationsBw.CancelAsync();
                servers[serverName].statisticsBw.CancelAsync();
                
                // Chiudi la connessione con il server
                if (servers[serverName].server.Connected)
                    servers[serverName].server.Close();

                // TODO: Pulisci interfaccia in questo caso
                safePulisciInterfaccia(serverName, false);

                return;
            }            

            string operation = null;
            string progName = null;
            
            while (serverStream.CanRead && server.Connected)
            {
                // Controlla se è stata chiamata CancelAsync() sul BackgroundThread che esegue riceiNotifiche 
                if (worker.CancellationPending == true)
                {
                    e.Cancel = true;
                    break;
                }
                                
                // Iinizio e dimensione messaggio: "--<4 byte int>-" = 7 byte in "buffer"
                // Leggi la dimensione del messaggio
                if (managedReadn(server, serverStream, serverName, buffer, 7) <= 0)
                    // Continue anche nei casi di server disconnesso oltre che ai timeout tanto in quei casi la condizione del while al prossimo giro non sarà soddisfatta
                    continue;
                int msgSize = IPAddress.NetworkToHostOrder(BitConverter.ToInt32(buffer, 2));

                // Leggi tutto il messaggio in "msg" => dimensione "msgSize"
                byte[] msg = new byte[msgSize];
                if (managedReadn(server, serverStream, serverName, msg, msgSize) <= 0)
                    continue;

                // Estrai operazione => primi 5 byte
                byte[] op = new byte[5];
                Array.Copy(msg, 0, op, 0, 5);
                operation = Encoding.ASCII.GetString(op);

                if (operation == "OKCLO")
                {
                    servers[serverName].notificationsBw.CancelAsync();
                    servers[serverName].statisticsBw.CancelAsync();

                    // Chiudi la connessione con il server
                    if (servers[serverName].server.Connected)
                        servers[serverName].server.Close();

                    safePulisciInterfaccia(serverName, true);
                    continue;   // continua perché venga visto alla prossima iterazione la CancellationPending == true
                }

                if (operation == "RETRY")
                {
                    continue;
                }

                if (operation == "ERRCL")
                {
                    servers[serverName].notificationsBw.CancelAsync();
                    servers[serverName].statisticsBw.CancelAsync();
                    System.Windows.MessageBox.Show("Il server ha chiuso la connessione in maniera inaspettata.");

                    // Chiudi la connessione con il server
                    if (servers[serverName].server.Connected)
                        servers[serverName].server.Close();

                    safePulisciInterfaccia(serverName, false);
                    continue;
                }

                // Estrai hwnd: successivi 5 byte.
                byte[] h = new byte[5];
                Array.Copy(msg, 6, h, 0, 4);
                int hwnd = BitConverter.ToInt32(msg, 6);

                // Estrai lunghezza nome programma => offset 6 (offset 5 è il '-' che precede)
                int progNameLength = IPAddress.NetworkToHostOrder(BitConverter.ToInt32(msg, 11));
                // Leggi nome del programma => da offset 11 (6 di operazione + 5 di dimensione (incluso 1 di trattino))
                byte[] pN = new byte[progNameLength];
                Array.Copy(msg, 5 + 5 + 6, pN, 0, progNameLength);
                progName = Encoding.Unicode.GetString(pN);

                /* Possibili valori ricevuti:
                    * --<4B dimensione messaggio>-FOCUS-<4B di HWND>-<4B per lunghezza nome prog>-<nome_nuova_app_focus>
                    * --<4B dimensione messaggio>-CLOSE-<4B di HWND>-<4B per lunghezza nome prog>-<nome_app_chiusa>
                    * --<4B dimensione messaggio>-TTCHA-<4B di HWND>-<4B per lunghezza nome prog>-<nome_app_con_nuovo_nome>
                    * --<4B dimensione messaggio>-OPENP-<4B di HWND>-<4B per lunghezza nome prog>-<nome_nuova_app_aperta>-<4B di dimensione icona>-<icona>
                    */

                switch (operation)
                {
                    case "FOCUS":
                        // Cambia programma col focus                            
                        servers[serverName].table.changeFocus(hwnd);

                        break;
                    case "CLOSE":
                        // Rimuovi programma dalla listView                            
                        servers[serverName].table.removeFinestra(hwnd);

                        break;
                    case "TTCHA":
                        // Cambia nome il nome della finestra ricevuta
                        servers[serverName].table.cambiaTitoloFinestra(hwnd, progName);

                        break;
                    case "OPENP":
                        try
                        {
                            /* Ricevi icona processo */
                            Bitmap bitmap = new Bitmap(64, 64);
                            bitmap.MakeTransparent(bitmap.GetPixel(1, 1));               // <-- TODO: Tentativo veloce di togliere lo sfondo nero all'icona
                                                                                            //bitmap.SetTransparencyKey(Color.White);

                            // Non ci interessano: 6 byte dell'operazione, il nome del programma, il trattino, 
                            // 4 byte di dimensione icona e il trattino
                            int notBmpData = 16 + progNameLength + 1 + 4 + 1;
                            int bmpLength = BitConverter.ToInt32(msg, notBmpData - 5);

                            /* Legge i successivi bmpLength bytes e li copia nel buffer bmpData */
                            byte[] bmpData = new byte[bmpLength];
                            Array.Clear(bmpData, 0, bmpLength);

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

                                // Aggiungi il nuovo elemento all'elenco delle tabelle
                                servers[serverName].table.addFinestra(hwnd, progName, "Background", 0, 0, bmpImage);

                            }
                        }
                        catch (Exception)
                        {
                            // qualsiasi eccezione relativa all'apertura di una nuova finestra, salta la finestra.
                            // Il buffer è stato ricevuto tutto, quindi si può continuare con le altre finestre                                    
                        }

                        break;
                }
                
            }
        }

        /* Chiamato al termine di aggiornaStatistiche */
        private void riceviNotificheTerminato(object sender, RunWorkerCompletedEventArgs e)
        {
            BackgroundWorker worker = sender as BackgroundWorker;

            if (e.Cancelled == true)
            {
                //System.Windows.MessageBox.Show("BackgroundWorker riceviNotifiche cancellato.");
            }
            else if (e.Error != null)
            {
                //System.Windows.MessageBox.Show("BackgroundWorker riceviNotifiche terminato con errori.");
            }
            else
            {
                //System.Windows.MessageBox.Show("BackgroundWorker riceviNotifiche terminato normalmente.");
            }
            
            worker.Dispose();            
        }

        delegate void PulisciInterfacciaDelegate(string disconnectingServer, bool onPurpose);

        /* Pulisce l'interfaccia capendo se è chiamata dal main thread o meno (richiesta di chiamare la Invoke)
         * Seleziona la listView adatta da visualizzare, con distinzione tra pulizia dovuta a disconnessione volontaria
         *      o pulizia dovuta a eccezzione scatenata (parametro onPurpose)
         * Disconnessione volontaria:   rimuove l'elenco delle finestre di quel server e la voce nella ListBox
         * Disconnessione forzata:      non rimuove l'elenco delle finestre di quel server né la voce nella ListBox          
         */
        private void safePulisciInterfaccia(string disconnectingServer, bool onPurpose)
        {
            if (!listView1.Dispatcher.CheckAccess())
            {
                // Non mi trovo sul main thread
                PulisciInterfacciaDelegate d = new PulisciInterfacciaDelegate(pulisciInterfacciaDopoDisconnessione);
                Dispatcher.BeginInvoke(d, new object[] { disconnectingServer, onPurpose });
            }
            else
            {
                // Mi trovo sul main thread e posso chiamare direttamente la funzione per aggiornare l'interfaccia
                pulisciInterfacciaDopoDisconnessione(disconnectingServer, onPurpose);
            }
        }

        private void pulisciInterfacciaDopoDisconnessione(string disconnectingServer, bool onPurpose)
        {
            if (onPurpose)
            {
                // Disconnessione volontaria: rimuovi server
                rimuoviServer(disconnectingServer);                

            }
            else if(servers.ContainsKey(disconnectingServer) && !onPurpose) // contiene disconnectingServer ed è forzata
            {
                // Disconnessione forzata: invalida server ma continua a mostrarlo nell'elenco
                invalidaServer(disconnectingServer);
                
            }

        }

        private void aggiungiServer(string serverName, ServerInfo si)
        {
            lock (servers)
            {
                if (!servers.ContainsKey(serverName))
                {
                    if (servers.Count == 0)
                    {
                        // Nella listBox c'è l'elemento "Nessun server connesso", quindi eliminalo
                        serversListBox.Items.RemoveAt(0);
                    }

                    servers.Add(serverName, si);

                    // Aggiungi e cambia la selezione della serversListBox al server appena connesso
                    int index = serversListBox.Items.Add(serverName);
                    serversListBox.SelectedIndex = index;
                    currentConnectedServer = serversListBox.Items[index] as string;
                }
                else
                {
                    if (servers[serverName].isOnline)
                    {
                        // Notifica che il server è già presente e connesso
                        System.Windows.MessageBox.Show("Già connessi al server " + serverName);
                        return;
                    }
                    else
                    {
                        // Server già presente ma offline: rimuovi i suoi riferimenti e riaggiungi
                        servers.Remove(serverName);
                        serversListBox.Items.Remove(serverName);
                        
                        servers.Add(serverName, si);

                        // Aggiungi e cambia la selezione della serversListBox al server appena connesso
                        int index = serversListBox.Items.Add(serverName);
                        serversListBox.SelectedIndex = index;
                        currentConnectedServer = serversListBox.Items[index] as string;
                        
                    }                    
                    
                }
            }
        }

        private void rimuoviServer(string serverName)
        {
            lock (servers)
            {
                if (servers.ContainsKey(serverName))
                {
                    // Rimuovi disconnectingServer da servers                
                    servers.Remove(serverName);
                    serversListBox.Items.Remove(serverName);

                    if (servers.Count == 0)
                    {
                        // non ci sono più server connessi
                        serversListBox.Items.Add("Nessun server connesso");
                        serversListBox.SelectedIndex = 0;

                        // Aggiorna bottoni
                        buttonDisconnetti.Visibility = Visibility.Hidden;
                        buttonInvia.IsEnabled = false;
                        buttonCattura.IsEnabled = false;

                    }
                    else
                    {
                        // Selezioniamo il primo server della lista
                        serversListBox.SelectedIndex = 0;
                    }
                }

            }
        }

        /* Disconnessione forzata: non eliminare voce dalla ListBox né l'elenco delle finestre. Disabilita e rimuovi tasti cattura.
         * Setta isOnline a false, per avvisare che quel server non è più direttamente collegato al client,
         * ma continuiamo a mostrare le statistiche all'ultima volta che è stato visto online
        */
        private void invalidaServer(string serverName)
        {
            servers[serverName].isOnline = false;
                        
            if (serverName.Equals(currentConnectedServer))
            {
                // L'elenco finestre disattivo è quello attivo, 
                // quindi mostra la label "Disconnesso", disabilita e nascondi cattura comando e permetti la chiusura dell'elenco finestre
                labelDisconnesso.Visibility = Visibility.Visible;
                disabilitaERimuoviCatturaComando();

                // Server non online: posso abilitare il pulsante "Chiudi server"
                buttonChiudiServer.IsEnabled = true;
                buttonChiudiServer.Visibility = Visibility.Visible;
                buttonDisconnetti.IsEnabled = false;
                buttonDisconnetti.Visibility = Visibility.Hidden;
            }
        }
   
        // Chiamato se il server mostrato è disconnesso e non si può abilitare la cattura del comando
        private void disabilitaERimuoviCatturaComando()
        {
            // Nascondi e disabilita tutto (caso server disconnesso)
            labelComando.Visibility = Visibility.Hidden;
            buttonCattura.Visibility = Visibility.Hidden;
            buttonAnnullaCattura.Visibility = Visibility.Hidden;

            textBoxComando.Visibility = Visibility.Hidden;
            buttonInvia.Visibility = Visibility.Hidden;

            textBoxComando.Text = "";
            comandoDaInviare.Clear();
            buttonCattura.IsEnabled = false;
            buttonAnnullaCattura.IsEnabled = false;
            buttonInvia.IsEnabled = false;
        }

        // Al click di "Cattura comando"
        private void abilitaCatturaComando()
        {
            // mostra e abilita annulla cattura
            buttonCattura.IsEnabled = false;
            buttonCattura.Visibility = Visibility.Hidden;
            buttonAnnullaCattura.Visibility = Visibility.Visible;
            buttonAnnullaCattura.IsEnabled = true;

            // mostra textBox e abilita invio
            textBoxComando.Visibility = Visibility.Visible;
            textBoxComando.Text = "";
            buttonInvia.Visibility = Visibility.Visible;
            buttonInvia.IsEnabled = true;

            // Crea event handler per scrivere i tasti premuti
            this.KeyDown += new System.Windows.Input.KeyEventHandler(OnButtonKeyDown);

        }

        // Al click di "Annulla cattura"
        private void disabilitaCatturaComando()
        {
            // Svuota la lista di keystroke da inviare
            comandoDaInviare.Clear();
            textBoxComando.Text = "";

            // mostra e abilita
            buttonCattura.IsEnabled = true;
            buttonCattura.Visibility = Visibility.Visible;
            buttonAnnullaCattura.Visibility = Visibility.Hidden;
            buttonAnnullaCattura.IsEnabled = false;

            // nascondi textBox e disabilita invio
            textBoxComando.Visibility = Visibility.Hidden;
            buttonInvia.Visibility = Visibility.Hidden;
            buttonInvia.IsEnabled = false;
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

        // Chiamato quando il server è online e si possono inviare comandi
        private void mostraCatturaComando()
        {
            // mostra cattura comando (caso server connesso, ma con cattura comando non abilitata)            
            labelComando.Visibility = Visibility.Visible;

            // buttonCattura abilitato e visibile
            buttonCattura.IsEnabled = true;
            buttonCattura.Visibility = Visibility.Visible;
            // buttonAnnullaCattura non visibile e disabilitato  
            buttonAnnullaCattura.Visibility = Visibility.Hidden;
            buttonAnnullaCattura.IsEnabled = false;

            // textBoxComando e buttonInvia non visibili
            textBoxComando.Visibility = Visibility.Hidden;
            buttonInvia.Visibility = Visibility.Hidden;
            buttonInvia.IsEnabled = false;
        }

        private void buttonDisconnetti_Click(object sender, RoutedEventArgs e)
        {
            disconnettiDalServer();
        }

        private void disconnettiDalServer()
        {
            NetworkStream serverStream = null;
            TcpClient server = null;

            // Definisci il server da disconnettere. 
            // Le strutture dati del server connesso in questo momento sono per forza lì, solo disconnettiDalServerWithBW le può rimuovere.
            // CurrentConnectedServer può essere però cambiato, quindi lo fissiamo in modo che ci si riferisca proprio a quello.
            string disconnectingServer = currentConnectedServer;

            try
            {
                byte[] buffer = new byte[9];
                Array.Clear(buffer, 0, 9);

                server = servers[disconnectingServer].server;
                serverStream = server.GetStream();

                // Prepara messaggio da inviare
                StringBuilder sb = new StringBuilder();
                sb.Append("--CLSCN-");
                Array.Copy(Encoding.ASCII.GetBytes(sb.ToString()), buffer, 8);
                buffer[8] = (byte)'\0';

                // Invia richiesta chiusura
                serverStream.Write(buffer, 0, 9);

            }
            catch (InvalidOperationException) // include ObjectDisposedException
            {
                // C'è stato un problema con il NetworkStream o nella CancenAsync().                
                // Sblocca il server manualmente chiamando CancelAsync().
                servers[disconnectingServer].notificationsBw.CancelAsync();
                servers[disconnectingServer].statisticsBw.CancelAsync();
            }
            catch (IOException)
            {
                // Sblocca il server manualmente chiamando CancelAsync().                
                servers[disconnectingServer].notificationsBw.CancelAsync();
                servers[disconnectingServer].statisticsBw.CancelAsync();
            }
        }

        private void buttonChiudiServerDisconnesso_Click(object sender, RoutedEventArgs e)
        {
            rimuoviServer(currentConnectedServer);
        }

        private void buttonCattura_Click(object sender, RoutedEventArgs e)
        {
            // Mostra la textBox dove scrivere e il button Invia
            abilitaCatturaComando();

            // Alternativa:
            //_hookID = SetHook(_proc);

        }

        private void OnButtonKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
        {
            // Se viene premuto Alt e.Key restituisce "System" ma la vera chiave di Alt è contenuta in SystemKey!
            Key pressedKey = (e.Key == Key.System ? e.SystemKey : e.Key);

            if (textBoxComando.Text.Length == 0)
            {
                textBoxComando.Text = pressedKey.ToString();
                buttonInvia.IsEnabled = true;
            }
            else
            {
                if (!textBoxComando.Text.Contains(pressedKey.ToString()))
                    textBoxComando.AppendText("+" + pressedKey.ToString());
            }

            // Converti c# Key in Virtual-Key da inviare al server
            comandoDaInviare.Add(KeyInterop.VirtualKeyFromKey(pressedKey));

            // Segnala l'evento come gestito per evitare che venga chiamata nuovamente OnButtonKeyDown
            e.Handled = true;
        }

        private void buttonInvia_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                byte[] messaggio;

                // Serializza messaggio da inviare
                StringBuilder sb = new StringBuilder();
                foreach (int virtualKey in comandoDaInviare)
                {
                    if (sb.Length != 0)
                        sb.Append("+");
                    sb.Append(virtualKey.ToString());
                    // System.Windows.MessageBox.Show(virtualKey.ToString());
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
                disabilitaCatturaComando();

                // Rimuovi event handler per non scrivere più i bottoni premuti nel textBox
                this.KeyDown -= new System.Windows.Input.KeyEventHandler(OnButtonKeyDown);
            }
            catch (InvalidOperationException) // include ObjectDisposedException
            {
                // C'è stato un problema con il NetworkStream o nella CancenAsync().
                System.Windows.MessageBox.Show("L'invio del comando non è anato a buon fine.");
                return;
            }
            catch (IOException)
            {
                // Problema nella write durante l'invio del comando
                System.Windows.MessageBox.Show("L'invio del comando non è anato a buon fine.");
                return;
            }
            catch(Exception)
            {
                // Problema generico nell'invio del comando
                System.Windows.MessageBox.Show("L'invio del comando non è anato a buon fine.");
                return;
            }
        }

        private void buttonAnnullaCattura_Click(object sender, RoutedEventArgs e)
        {
            disabilitaCatturaComando();

            //UnhookWindowsHookEx(_hookID);
        }

        private void serversListBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            string selectedServer = ((sender as System.Windows.Controls.ListBox).SelectedItem as string);
                        
            if (selectedServer != null)
            {
                if (!selectedServer.Equals("Nessun server connesso") && servers[selectedServer].isOnline)
                {
                    // Aggiorna elenco finestre
                    currentConnectedServer = selectedServer;
                    listView1.ItemsSource = servers[selectedServer].table.Finestre;

                    // Server online: posso abilitare il pulsante "Disconnetti"
                    buttonDisconnetti.IsEnabled = true;
                    buttonDisconnetti.Visibility = Visibility.Visible;
                    buttonChiudiServer.IsEnabled = false;
                    buttonChiudiServer.Visibility = Visibility.Hidden;
                    
                    indirizzoServerConnesso.Content = servers[selectedServer].serverName;

                    labelDisconnesso.Visibility = Visibility.Hidden;

                    mostraCatturaComando();

                }
                else if (!selectedServer.Equals("Nessun server connesso") && !servers[selectedServer].isOnline)
                {
                    // Aggiorna elenco finestre
                    currentConnectedServer = selectedServer;
                    listView1.ItemsSource = servers[selectedServer].table.Finestre;

                    // Server non online: posso abilitare il pulsante "Chiudi server"
                    buttonChiudiServer.IsEnabled = true;
                    buttonChiudiServer.Visibility = Visibility.Visible;
                    buttonDisconnetti.IsEnabled = false;
                    buttonDisconnetti.Visibility = Visibility.Hidden;
                    
                    indirizzoServerConnesso.Content = servers[selectedServer].serverName;

                    labelDisconnesso.Visibility = Visibility.Visible;

                    // per disabilitare e rimuovere la cattura dei comandi 
                    disabilitaERimuoviCatturaComando();

                }
                else if (selectedServer.Equals("Nessun server connesso"))
                {
                    // E' selezionato "Nessun server connesso"
                    currentConnectedServer = selectedServer;
                    listView1.ItemsSource = null;

                    // Non mostrare né "Disconnetti" né "Chiudi server"
                    buttonChiudiServer.IsEnabled = false;
                    buttonChiudiServer.Visibility = Visibility.Hidden;
                    buttonDisconnetti.IsEnabled = false;
                    buttonDisconnetti.Visibility = Visibility.Hidden;
                    
                    indirizzoServerConnesso.Content = "Nessun server connesso";

                    // Nascondi label "Disconnesso"
                    labelDisconnesso.Visibility = Visibility.Hidden;

                    // per disabilitare e rimuovere la cattura dei comandi 
                    disabilitaERimuoviCatturaComando();

                }
            }

        }

        // Normalmente ritorna quanti byte ha letto, altrimenti 0 se dopo 1 secondo non ha letto niente oppure -1 se si è scatenata un'eccezione
        private int readn(TcpClient server, NetworkStream serverStream, String serverName, byte[] buffer, int n)
        {
            int offset = 0;
            try
            {
                while (n > 0)
                {
                    if (server.Client.Poll(1000000 /* MICROseconds */, SelectMode.SelectRead))
                    {
                        int read = serverStream.Read(buffer, offset, n);
                        if (read == 0)  // Significa che la connessione è stata chiusa
                            return -2;  // TODO: non è bellissimo
                        n -= read;
                        offset += read;
                    }
                    else
                    {
                        return 0;
                    }
                }
                return offset;
            }
            catch (Exception e)
            {
                if (e is IOException)
                {
                    // Scatenata dalla Read(): il socket è stato chiuso lato server.
                    // Setta disconnectionEvent e continua per uscire dal ciclo alla prossima iterazione.
                }
                else if (e is ObjectDisposedException)
                {
                    // Il networkStream è stato chiuso oppure c'è stato un errore nella lettura dalla rete.
                    // Setta disconnectionEvent e continua per uscire dal ciclo alla prossima iterazione.
                }
                else
                {
                    // qualsiasi eccezione sia stata sctenata, il thread è necessario:
                    // prova a riavviarlo, oppure...
                    throw e;
                }

                return -1;
            }
        }

        private int managedReadn(TcpClient server, NetworkStream serverStream, String serverName, byte[] buffer, int n)
        {
            int res;
            if ((res = readn(server, serverStream, serverName, buffer, n)) == 0)
            {
                // È stato superato il timeout senza ricevere niente
                return res;
            }
            else if (res == -2)
            {
                // La read ha letto 0 byte, significa che il server non è più raggiungibile, posso chiudere tutto                
                System.Windows.MessageBox.Show("Il server ha chiuso la connessione in maniera inaspettata.");
                servers[serverName].statisticsBw.CancelAsync();
                servers[serverName].notificationsBw.CancelAsync();

                safePulisciInterfaccia(serverName, false);
                return res;
            }
            else if (res < 0)
            {
                // Eccezione scatenata in readn
                System.Windows.MessageBox.Show("Il server ha chiuso la connessione in maniera inaspettata.");
                servers[serverName].statisticsBw.CancelAsync();
                servers[serverName].notificationsBw.CancelAsync();

                safePulisciInterfaccia(serverName, false);
                return res;
            }
            return res;
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
