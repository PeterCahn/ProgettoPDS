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

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using System.Threading.Tasks;


/* TODO:
 * - Distruttore
 * - Icona con sfondo nero
 *      -> Confermato che il problema è lato server perchè spostando makeTransprent a dopo aver impostato il bitmap ai dati ricevuti dal server, il client mostra effettivamente uno sfondo
 *         trasparente e quindi è in grado di farlo. È quindi l'immagine sorgente che ha lo sfondo nero, e viene dal server. 
 *         Oltre a questo l'immagine sorgente, per colpa dello sfondo, ha forma quadrata e non quella dell'icona, e quando si passa il mouse sopra all'elemento corrispondente nella listView questo si vede 
 * - Cattura comandi: al momento si possono evitare di attivare funzioni di Windows premendo un tasto alla volta invece che tutti insieme, ma questo non vale per il tasto Win. Aggiustare?
 * - Il cursore della textbox catturaComando deve sempre mettersi a destra
 */

namespace WpfApplication1
{
    public partial class MainWindow : Window
    {
        private const int FREQUENZA_AGGIORNAMENTO_STATISTICHE = 500;
        private static List<string> comandoDaInviare = new List<string>();
        private string currentConnectedServer;
        private Dictionary<string, ServerInfo> servers = new Dictionary<string, ServerInfo>();
        private List<int> commandsList = new List<int>();
        private Boolean disconnessioneInCorso;      // utile al fine di non stampare "il client ha chiuso la connessione improvvisamente" quando 
                                                    // si preme Disconnetti e la managedReadn() si vede il socket improvvisamente chiuso
        private object disconnessioneInCorso_lock = new object();   // oggetto su cui facciamo lock prima di accedere al Boolean disconnessioneInCorso
        private Boolean catturandoComandi = false;  // utile per rimuovere i vari handler (keydown/previewKeydown/keyup) solo se precedentemente aggiunti

        /* BackgroundWorker necessario per evitare che il main thread si blocchi 
         * mentre aspetta che si instauri la connessione con un nuovo server. 
         * Situazione tipo: non è possibile connettersi al server finché non scade il timeout di connessione nella TcpClient. */
        private BackgroundWorker bw = new BackgroundWorker();
        private bool timedOut;

        // Per gestire la possibilità di terminare il tentativo di connessione in corso
        private ManualResetEvent terminaConnessione = new ManualResetEvent(false);
        private bool terminata = false;

        private string connectingIp;
        private int connectingPort;

        /* Handler eventi alla pressione dei tasti durante la cattura di un comando */
        System.Windows.Input.KeyEventHandler keyDownHandler;
        System.Windows.Input.KeyEventHandler keyUpHandler;
        System.Windows.Input.KeyEventHandler previewKeyDownHandler;
        
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

            // Definisci gli event handler per la gestione della pressione dei tasti durante la cattura di un comando
            keyDownHandler = new System.Windows.Input.KeyEventHandler(onButtonKeyDown);
            keyUpHandler = new System.Windows.Input.KeyEventHandler(onButtonKeyUp);
            previewKeyDownHandler = new System.Windows.Input.KeyEventHandler(onButtonPreviewKeyDown);
        }

        ~MainWindow()
        {
            System.Windows.MessageBox.Show("Nel distruttore di MainWindow", "Client - Avviso");
            // TODO: aggiungere cose
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
            iniziaConnessione();    // connessione asincrona
        }

        private void buttonTerminaConnessione_Click(object sender, RoutedEventArgs e)
        {
            terminaConnessione.Set();    // connessione asincrona

            terminata = true;
            
            buttonTerminaConnessione.IsEnabled = false;
            buttonTerminaConnessione.Visibility = Visibility.Hidden;

        }

        private void iniziaConnessione()
        {
            IPEndPoint ipPort = null;
            string ipAddress = null;
            int port = -1;
            string serverName = null;

            lock(disconnessioneInCorso_lock)
                disconnessioneInCorso = false;

            /* Ottieni il l'indirizzo IP e la porta a cui connettersi. */
            ipPort = parseHostPort(textBoxIpAddress.Text);
            if (ipPort == null)
            {
                System.Windows.MessageBox.Show("Formato ammesso: [0-255].[0-255].[0-255].[0.255]:[1024-65535]", "Client - Avviso");
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
                        System.Windows.MessageBox.Show("Già connessi al server " + serverName, "Client - Avviso");
                        return;
                    }
                }
            }

            int timesRetried = 0;
            while (timesRetried < 10)
            {
                if (bw.IsBusy != true)
                {
                    connectingIp = ipAddress;
                    connectingPort = port;

                    textBoxIpAddress.IsEnabled = false;
                    buttonConnetti.IsEnabled = false;

                    //buttonTerminaConnessione.IsEnabled = true;
                    //buttonTerminaConnessione.Visibility = Visibility.Visible;

                    bw.RunWorkerAsync();
                    break;
                }
                else
                {
                    timesRetried++;
                    Thread.Sleep(50);
                }
            }
            if (timesRetried >= 10)
                System.Windows.MessageBox.Show("Errore nella connessione al server, riprovare", "Client - Avviso");
        }

        private void provaConnessione(object sender, DoWorkEventArgs e)
        {
            try
            {
                TcpClient connection = new TcpClient();
                connection.ExclusiveAddressUse = true;

                /* Tentativo di connessione asincrona */
                //var x = connection.BeginConnect(connectingIp, connectingPort, new AsyncCallback(finisciConnect), connection);

                /* Aspetta finché non termino manualmente la connessione oppure la 'finisiConnect' ha terminato con o senza eccezioni */
                //terminaConnessione.WaitOne();

                /* Come facevamo prima: non succede niente quando termina la wait perché nessuna eccezione viene generata, e l'esecuzione continua */
                timedOut = connection.ConnectAsync(connectingIp, connectingPort).Wait(7000);

                if (!timedOut)
                    throw new SocketException((int) SocketError.TimedOut);

                /* Metti TcpClient in Result per poter essere controllato */
                e.Result = connection;

                /* ArgumentNullException: hostname is null
                 * ArgumentOutOfRangeException: port non è tra MinPort e MaxPort */

            }
            catch (SocketException se)
            {
                int errorCode = se.ErrorCode;
                if (errorCode.Equals(SocketError.TimedOut))
                    System.Windows.MessageBox.Show("Tentativo di connessione al server " + connectingIp + ":" + connectingPort + " scaduto.", "Client - Avviso");
                else
                    System.Windows.MessageBox.Show("Connessione al server " + connectingIp + ":" + connectingPort + " fallita.", "Client - Avviso");

                return; // Usciamo perché l'operazione non è andata a buon fine. Il nuovo tentativo sarà manuale.
            }
            catch(ObjectDisposedException)
            {
                System.Windows.MessageBox.Show("Tentativo di connessione al server " + connectingIp + ":" + connectingPort + " fallito.", "Client - Avviso");
                return;
            }
            catch (AggregateException ae)
            {
                // Il task è stato cancellato se AggregateException.InnerException contiene un TaskCanceledException
                // Gestiamo solo il caso di SocketException per capire se c'è stato un timeout.                

                ae.Handle(ex => {                    

                    if (ex is SocketException)
                    {
                        SocketException exception = (SocketException) ex;
                        int errorCode = exception.ErrorCode;

                        if (errorCode.Equals(SocketError.TimedOut))
                            System.Windows.MessageBox.Show("Tentativo di connessione al server " + connectingIp + ":" + connectingPort + " scaduto.", "Client - Avviso");
                        else
                            System.Windows.MessageBox.Show("Connessione al server " + connectingIp + ":" + connectingPort + " fallita.", "Client - Avviso");
                    }
                    else if(ex is Exception)
                    {
                        System.Windows.MessageBox.Show("Tentativo di connessione al server " + connectingIp + ":" + connectingPort + " fallito.\nImpossibile attendere la connessione.", "Client - Avviso");
                    }

                    return ex is SocketException;
                });

                return;
            }
            catch(Exception)
            {
                System.Windows.MessageBox.Show("Tentativo di connessione al server " + connectingIp + ":" + connectingPort + " fallito.\nImpossibile attendere la connessione.", "Client - Avviso");
                return;
            }
            finally
            {
                terminaConnessione.Reset();
                terminata = false;
            }

        }

        private void finisciConnect(IAsyncResult ar)
        {
            TcpClient t = (TcpClient)ar.AsyncState;

            try
            {
                t.EndConnect(ar);

                terminaConnessione.Set();
            }
            catch (SocketException se)
            {
                if (!terminata)
                {
                    int errorCode = se.ErrorCode;
                    if (errorCode.Equals(SocketError.TimedOut))
                        System.Windows.MessageBox.Show("Tentativo di connessione al server " + connectingIp + ":" + connectingPort + " scaduto.", "Client - Avviso");
                    else
                        System.Windows.MessageBox.Show("Connessione al server " + connectingIp + ":" + connectingPort + " fallita.", "Client - Avviso");
                }
                return; // Usciamo perché l'operazione non è andata a buon fine. Il nuovo tentativo sarà manuale.
            }
            catch (ObjectDisposedException)
            {
                if(!terminata)
                    System.Windows.MessageBox.Show("Tentativo di connessione al server " + connectingIp + ":" + connectingPort + " fallito.", "Client - Avviso");

                return;
            }
            catch (Exception)
            {
                if(!terminata)
                    System.Windows.MessageBox.Show("Tentativo di connessione al server " + connectingIp + ":" + connectingPort + " fallito.", "Client - Avviso");

                return;
            }
            finally
            {
                terminaConnessione.Set();
            }

        }

        private void finalizzaConnessione(object sender, RunWorkerCompletedEventArgs e)
        {
            TcpClient s = (TcpClient)e.Result;

            if (s == null || !s.Connected)
            {
                // Non è stato possibile connettersi, quindi ritorna
                textBoxIpAddress.IsEnabled = true;
                buttonConnetti.IsEnabled = true;

                //buttonTerminaConnessione.IsEnabled = false;
                //buttonTerminaConnessione.Visibility = Visibility.Hidden;

                return;
            }
            else
            {
                // Connessione riuscita, riabilita i pulsanti per connettersi a un nuovo server
                textBoxIpAddress.IsEnabled = true;
                buttonConnetti.IsEnabled = true;

                //buttonTerminaConnessione.IsEnabled = false;
                //buttonTerminaConnessione.Visibility = Visibility.Hidden;
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

        /* Metodo passato al BackgroundWorker per aggiornare le statistiche */
        private void aggiornaStatistiche(object sender, DoWorkEventArgs e)
        {
            string serverName = (string)e.Argument;
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
                    if (servers.ContainsKey(serverName)) // l'argomento non può essere null perché validato
                    {
                        servers[serverName].table.aggiornaStatisticheFocus();
                    }

                    Thread.Sleep(FREQUENZA_AGGIORNAMENTO_STATISTICHE);
                }
            }
        }

        /* Chiamato al termine di aggiornaStatistiche */
        private void aggiornaStatisticheTerminato(object sender, RunWorkerCompletedEventArgs e)
        {
            BackgroundWorker worker = sender as BackgroundWorker;
            worker.Dispose();
        }

        /* Come manageNotifications ma da eseguire in un BackgroundWorker */
        private void riceviNotifiche(object sender, DoWorkEventArgs e)
        {
            string serverName = (string)e.Argument;
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
                System.Windows.MessageBox.Show("Problema inaspettato durante la ricezione delle notifiche.\nArresto ricezione notifiche per il server " + serverName + ".", "Client - Avviso");
                servers[serverName].notificationsBw.CancelAsync();
                servers[serverName].statisticsBw.CancelAsync();

                // Chiudi la connessione con il server
                if (servers[serverName].server.Connected)
                    servers[serverName].server.Close();

                // TODO: Pulisci interfaccia in questo caso
                safePulisciInterfaccia(serverName, false);

                return;
            }
            catch (Exception)
            {
                /* Eccezione scatenata se serverName non c'è più in 'servers' */
                System.Windows.MessageBox.Show("Problema inaspettato durante la ricezione delle notifiche.\nArresto ricezione notifiche per il server " + serverName + ".", "Client - Avviso");
                servers[serverName].notificationsBw.CancelAsync();
                servers[serverName].statisticsBw.CancelAsync();

                // Chiudi la connessione con il server
                if (servers[serverName].server.Connected)
                    servers[serverName].server.Close();

                safePulisciInterfaccia(serverName, false);

                return;
            }

            while (serverStream.CanRead && server.Connected)
            {
                string operation = "";
                string progName = "";

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

                string json = Encoding.UTF8.GetString(msg);
                JToken token = JObject.Parse(json);

                int hwnd = 0;
                operation = (string)token.SelectToken("operation");

                /* Possibili valori ricevuti:
                    * --<4B dimensione messaggio>-FOCUS-<4B di HWND>-<4B per lunghezza nome prog>-<nome_nuova_app_focus>
                    * --<4B dimensione messaggio>-CLOSE-<4B di HWND>-<4B per lunghezza nome prog>-<nome_app_chiusa>
                    * --<4B dimensione messaggio>-TTCHA-<4B di HWND>-<4B per lunghezza nome prog>-<nome_app_con_nuovo_nome>
                    * --<4B dimensione messaggio>-OPENP-<4B di HWND>-<4B per lunghezza nome prog>-<nome_nuova_app_aperta>-<4B di dimensione icona>-<icona>
                    */
                switch (operation)
                {
                    case "RETRY":
                        break;
                    case "ERRCL":
                        servers[serverName].notificationsBw.CancelAsync();
                        servers[serverName].statisticsBw.CancelAsync();
                        System.Windows.MessageBox.Show("Il server ha chiuso la connessione in maniera inaspettata.", "Client - Avviso");

                        // Chiudi la connessione con il server
                        if (servers[serverName].server.Connected)
                            servers[serverName].server.Close();

                        safePulisciInterfaccia(serverName, false);
                        //continue;
                        break;

                    case "FOCUS":
                        hwnd = (int)token.SelectToken("hwnd");
                        // Cambia programma col focus                            
                        servers[serverName].table.changeFocus(hwnd);

                        break;
                    case "CLOSE":
                        hwnd = (int)token.SelectToken("hwnd");
                        // Rimuovi programma dalla listView                            
                        servers[serverName].table.removeFinestra(hwnd);

                        break;
                    case "TTCHA":
                        hwnd = (int)token.SelectToken("hwnd");
                        progName = Encoding.Unicode.GetString(Convert.FromBase64String(token.SelectToken("windowName").ToString()));

                        servers[serverName].table.cambiaTitoloFinestra(hwnd, progName);

                        break;
                    case "OPEN":
                        try
                        {
                            hwnd = (int)token.SelectToken("hwnd");

                            progName = Encoding.Unicode.GetString(Convert.FromBase64String(token.SelectToken("windowName").ToString()));
                            string iconaBase64 = token.SelectToken("icona").ToString();
                            byte[] bmpData = Convert.FromBase64String(iconaBase64);

                            /* Ricevi icona processo */ // 64*64 se si usa Helper::ottieniIcona() sul server
                            int bitmapWidth = 32;
                            int bitmapheight = 32;

                            /* Crea la bitmap a partire dal byte array */
                            Bitmap bitmap = CopyDataToBitmap(bmpData, bitmapWidth, bitmapheight);


                            /* Il bitmap è salvato in memoria sottosopra, va raddrizzato */
                            bitmap.RotateFlip(RotateFlipType.RotateNoneFlipY);

                            //bitmap.MakeTransparent(bitmap.GetPixel(1, 1));               // <-- TODO: Tentativo veloce di togliere lo sfondo nero all'icona
                            //bitmap.MakeTransparent(Color.Black);
                            //bitmap.SetTransparencyKey(Color.White);

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
                            continue;
                        }

                        break;
                }

                if (operation == "ERRCL" || operation == "OKCLO" || operation == "RETRY")
                    continue;

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

        /* Chiama funzione per pulire l'interfaccia capendo se è chiamata dal main thread o meno (richiesta di chiamare la Invoke)         
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

        /* Distinzione tra pulizia dovuta a disconnessione volontaria
         *      o pulizia dovuta a eccezione scatenata (parametro onPurpose)
         * Disconnessione volontaria:   rimuove l'elenco delle finestre di quel server e la voce nella ListBox
         * Disconnessione forzata:      non rimuove l'elenco delle finestre di quel server né la voce nella serversListBox          
         */
        private void pulisciInterfacciaDopoDisconnessione(string disconnectingServer, bool onPurpose)
        {
            if (onPurpose)
            {
                // Disconnessione volontaria: rimuovi server
                rimuoviServer(disconnectingServer);

            }
            else if (servers.ContainsKey(disconnectingServer) && !onPurpose) // contiene disconnectingServer ed è forzata
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
                        System.Windows.MessageBox.Show("Già connessi al server " + serverName, "Client - Avviso");
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

            // Crea event handler per scrivere i tasti premuti
            this.KeyDown += keyDownHandler;
            this.KeyUp += keyUpHandler;
            this.PreviewKeyDown += previewKeyDownHandler;
            catturandoComandi = true;   // Indica che i nuovi handler sono stati aggiunti
            //_hookID = SetHook(_proc);
        }

        // Chiamato se il server mostrato è disconnesso e non si può abilitare la cattura del comando
        private void disabilitaERimuoviCatturaComando()
        {
            // Nascondi e disabilita tutto (caso server disconnesso)
            disabilitaCatturaComando();
            buttonCattura.Visibility = Visibility.Hidden;
            labelComando.Visibility = Visibility.Hidden;
        }

        // Al click di "Annulla cattura"
        private void disabilitaCatturaComando()
        {
            // Svuota la lista di keystroke da inviare
            comandoDaInviare.Clear();
            textBoxComando.Text = "";

            // mostra e abilita
            buttonCattura.IsEnabled = true;
            buttonCattura.Visibility = Visibility.Visible;  //
            buttonAnnullaCattura.Visibility = Visibility.Hidden;
            buttonAnnullaCattura.IsEnabled = false;

            // nascondi textBox e disabilita invio
            textBoxComando.Visibility = Visibility.Hidden;

            // Rimuovi handler eventi keydown/previewKeydown/keyup
            if (catturandoComandi)
            {
                this.KeyDown -= keyDownHandler;
                this.KeyUp -= keyUpHandler;
                this.PreviewKeyDown -= previewKeyDownHandler;
            }
            catturandoComandi = false;  // Indica che gli handler sono stati rimossi

            commandsList.Clear();
        }

        public Bitmap CopyDataToBitmap(byte[] data, int width, int height)
        {
            // Here create the Bitmap to the know height, width and format
            Bitmap bmp = new Bitmap(width, height, PixelFormat.Format32bppRgb);

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

            //UnhookWindowsHookEx(_hookID);
        }

        private void buttonDisconnetti_Click(object sender, RoutedEventArgs e)
        {
            disconnettiDalServer();
        }

        private void disconnettiDalServer()
        {
            NetworkStream serverStream = null;
            TcpClient server = null;
            lock(disconnessioneInCorso_lock)
                disconnessioneInCorso = true;

            // Definisci il server da disconnettere.
            // CurrentConnectedServer può essere cambiato, quindi lo fissiamo in modo che ci si riferisca proprio a quello.
            string disconnectingServer = currentConnectedServer;

            try
            {
                server = servers[disconnectingServer].server;
                serverStream = server.GetStream();

                BinaryWriter bw = new BinaryWriter(serverStream);

                JObject jo = new JObject();
                jo.Add("operation", "CLSCN");
                string message = jo.ToString(Formatting.None);
                message += '\0';

                bw.Write(message.ToCharArray(), 0, message.Length);

                servers[disconnectingServer].notificationsBw.CancelAsync();
                servers[disconnectingServer].statisticsBw.CancelAsync();

                // Chiudi la connessione con il server
                if (servers[disconnectingServer].server.Connected)
                    servers[disconnectingServer].server.Close();

                safePulisciInterfaccia(disconnectingServer, true);

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

        private void onButtonKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
        {
            if (!e.IsRepeat)
            {
                lock (comandoDaInviare)
                {
                    int virtualKey = KeyInterop.VirtualKeyFromKey(e.Key);
                    comandoDaInviare.Add(virtualKey + "+");
                    commandsList.Add(virtualKey);
                    textBoxComando.AppendText(e.Key.ToString() + "+");
                }
            }

            // Segnala l'evento come gestito per evitare che venga chiamata nuovamente OnButtonKeyDown
            e.Handled = true;
        }

        private void onButtonKeyUp(object sender, System.Windows.Input.KeyEventArgs e)
        {
            // Se viene premuto Alt e.Key restituisce "System" ma la vera chiave di Alt è contenuta in SystemKey!
            Key pressedKey = (e.Key == Key.System ? e.SystemKey : e.Key);

            lock (comandoDaInviare)
            {
                int virtualKey = KeyInterop.VirtualKeyFromKey(pressedKey);
                comandoDaInviare.Add(virtualKey + "-");
                if(commandsList.Contains(virtualKey))
                    commandsList.Remove(virtualKey);
                else
                {
                    /* Comando malformato / Combinazione di Windows:
                     * I comandi di questo tipo, dove il client intercetta l'evento KeyUp di un tasto ma non il precedente keyDown, sono
                     * solitamente quelli che rappresentano delle combinazioni intercettate da Windows. Il sistema operativo infatti intercettando
                     * l'evento KeyDown precedente al KeyUp impedisce al client di vederlo. In questi casi, essendo queste delle combinazioni di 
                     * Windows che possono essere utili lato client, non facciamo inviare la combinazione al server.
                     */
                    string combinazione = "";
                    foreach (int vKey in commandsList) {
                        if (combinazione.Length > 0)
                            combinazione += " + ";
                        combinazione += KeyInterop.KeyFromVirtualKey(vKey).ToString();
                    }
                    System.Windows.MessageBox.Show("Impossibile inviare questo comando (" + combinazione + " + " + pressedKey + ")", "Client - Avviso");
                    disabilitaCatturaComando();
                    e.Handled = true;
                    return;
                }
                textBoxComando.AppendText(pressedKey.ToString() + "-");
                if (commandsList.Count == 0)
                    buttonInvia_Click();
            }

            e.Handled = true;
        }

        private void onButtonPreviewKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
        {
            /* Nel caso della presenza di modifiers l'oggetto e.Key è leggibile correttamente solo nella perviewKeyDown e non nella keyDown, quindi gestiamo qui questo caso */
            if (!e.IsRepeat && (e.SystemKey != System.Windows.Input.Key.None) && (e.KeyboardDevice.Modifiers & ModifierKeys.Alt) == ModifierKeys.Alt)
            {
                lock (comandoDaInviare)
                {
                    int virtualKey = KeyInterop.VirtualKeyFromKey(e.SystemKey);
                    comandoDaInviare.Add(virtualKey + "+");
                    commandsList.Add(virtualKey);
                    textBoxComando.AppendText(e.SystemKey.ToString() + "+");
                }
                e.Handled = true;
            }
            if (!e.IsRepeat && ((e.KeyboardDevice.Modifiers & ModifierKeys.Control) == ModifierKeys.Control 
                || (e.KeyboardDevice.Modifiers & ModifierKeys.Shift) == ModifierKeys.Shift
                || (e.KeyboardDevice.Modifiers & ModifierKeys.Windows) == ModifierKeys.Windows))
            {
                lock (comandoDaInviare)
                {
                    int virtualKey = KeyInterop.VirtualKeyFromKey(e.Key);
                    comandoDaInviare.Add(virtualKey + "+");
                    commandsList.Add(virtualKey);
                    textBoxComando.AppendText(e.Key.ToString() + "+");
                }
                e.Handled = true;
            }
        }

        private void buttonInvia_Click(/*object sender, RoutedEventArgs e*/)
        {
            try
            {
                int currentHwnd;

                lock (servers)
                {
                    currentHwnd = servers[currentConnectedServer].table.handleFinestraInFocus();
                }

                JObject jsonTasti = new JObject();
                jsonTasti.Add("operation", "comando");
                jsonTasti.Add("hwnd", currentHwnd);

                // Prepara messaggio da inviare
                StringBuilder sb = new StringBuilder();

                foreach (string virtualKey in comandoDaInviare)
                {
                    sb.Append(virtualKey.ToString());
                }
                jsonTasti.Add("tasti", sb.ToString());

                /* Prepara l'invio del messaggio */
                NetworkStream serverStream = null;

                TcpClient server = null;
                server = servers[currentConnectedServer].server;
                serverStream = server.GetStream();

                BinaryWriter bw = new BinaryWriter(serverStream);

                string message = jsonTasti.ToString(Formatting.None);
                message += '\0';

                bw.Write(message.ToCharArray(), 0, message.Length);

                // Invia messaggio
                //serverStream.Write(messaggio, 0, messaggio.Length);

                // Aggiorna bottoni e textBox
                // disabilitaCatturaComando();

                // Preparati per prossimo keystroke
                comandoDaInviare.Clear();
                textBoxComando.Text = "";
                commandsList.Clear();

            }
            catch (InvalidOperationException) // include ObjectDisposedException
            {
                // C'è stato un problema con il NetworkStream o nella CancenAsync().
                System.Windows.MessageBox.Show("L'invio del comando non è anato a buon fine.", "Client - Avviso");
                return;
            }
            catch (IOException)
            {
                // Problema nella write durante l'invio del comando
                System.Windows.MessageBox.Show("L'invio del comando non è anato a buon fine.", "Client - Avviso");
                return;
            }
            catch (Exception)
            {
                // Problema generico nell'invio del comando
                System.Windows.MessageBox.Show("L'invio del comando non è anato a buon fine.", "Client - Avviso");
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
                return 0;
            }
            else if (res < 0)
            {
                lock (disconnessioneInCorso_lock)
                {
                    if (!disconnessioneInCorso) // Settato a true se la disconnessione è volontaria (premendo il button Disconnetti)
                    {
                        // Eccezione scatenata in readn
                        System.Windows.MessageBox.Show("Il server ha chiuso la connessione in maniera inaspettata.", "Client - Avviso");
                        servers[serverName].statisticsBw.CancelAsync();
                        servers[serverName].notificationsBw.CancelAsync();
                    }
                }

                safePulisciInterfaccia(serverName, false);
                return res;
            }
            return res;
        }


        /* Disattiva la cattura di un comando quando la finestra del client perde il focus.
         * Ottenuto tramite la gestione dell'evento Deactivated di Window
         */
        private void Window_Deactivated(object sender, EventArgs e)
        {
            disabilitaCatturaComando();
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
                //Console.WriteLine((Keys)vkCode);
                //System.Windows.MessageBox.Show(vkCode.ToString());  // TODO: Eliminando questo invia più tasti.

                comandoDaInviare.Add(vkCode + "+"); // TODO: rivedi prima di usare (SEEE)

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
