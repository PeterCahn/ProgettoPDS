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
using System.Windows.Forms;


/* TODO:
 * - Distruttore (utile ad esempio per fare Mutex.Dispose())
 */

/* DA CHIARIRE:
 * - Quando chiudo una finstra, se questa ha una percentuale != 0, siccome la sua riga viene eliminata dalla listView le percentuali non raggiungono più il 100%:
 *   siccome c'è scritto di mostrare la percentuale di tempo in cui una finstra è stata in focus dal momento della connessione, a me questo comportamento anche se
 *   strano sembra corretto. Boh, vedere un po' di chiarire.
 * - Eccessiva lentezza nel ricevere e aggiornare la lista delle finestre aperte sul server. Possibile riduzione dimensione icona inviata lato server.
 */

namespace WpfApplication1
{
    class ListViewRow
    {
        public BitmapImage Icona { get; set; } // WPF accetta, tra gli altri, Bitmapimage, non Bitmap puro
        public string Nome { get; set; }
        public string Stato { get; set; }
        public string PercentualeFocus { get; set; }
        public float TempoFocus { get; set; }
    }

    public partial class MainWindow : Window
    {
        private byte[] buffer = new byte[1024];
        private Socket sock;
        private Thread statisticsThread;
        private Thread notificationsThread;
        private List<int> comandoDaInviare = new List<int>();
        private ImageList imageList = new ImageList();     

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
                IPAddress ipAddr = IPAddress.Parse(textBoxIpAddress.Text);


                // Crea endpoint a cui connettersi
                IPEndPoint ipEndPoint = new IPEndPoint(ipAddr, 27015);

                // Crea socket
                sock = new Socket(ipAddr.AddressFamily, SocketType.Stream, ProtocolType.Tcp);

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

                // Avvia thread per notifiche
                notificationsThread = new Thread(() => manageNotifications(sock));      // lambda perchè è necessario anche passare il parametro
                notificationsThread.IsBackground = true;
                notificationsThread.Start();

               // Avvia thread per statistiche live
                statisticsThread = new Thread(new ThreadStart(this.manageStatistics));
                statisticsThread.IsBackground = true;
                statisticsThread.Start();
            }
            catch (Exception exc)
            {
                textBoxStato.AppendText("\nECCEZIONE: " + exc.ToString());
                textBoxStato.ScrollToEnd();
            }
        }

        void addItemToListView(string nomeProgramma, BitmapImage bmp)
        {
            if (listView1Mutex.WaitOne(1000))
            {
                listView1.Items.Add(new ListViewRow() { Icona = bmp, Nome = nomeProgramma, Stato = "Background", PercentualeFocus = "0", TempoFocus = 0 });                
                listView1Mutex.ReleaseMutex();
            }
        }

        /* Viene eseguito in un thread a parte.
         * Si occupa della gestione delle statistiche, aggiornando le percentuali di Focus ogni 500ms.
         * In questo modo abbiamo statistiche "live"
         */
        private void manageStatistics()
        {
            // Imposta tempo connessione
            DateTime connectionTime = DateTime.Now;
            try
            {
                DateTime lastUpdate = DateTime.Now;

                // Sleep() necessario per evitare divisione per 0 alla prima iterazione e mostrare NaN per il primo mezzo secondo nelle statistiche
                //      Piero: senza sleep (dopo aver messo l'icona), non viene mostrato nessun NaN
                //Thread.Sleep(1);
                while (true)
                {
                    if (listView1Mutex.WaitOne(1000))
                    {
                        foreach (ListViewRow item in listView1.Items)
                        {
                            if (item.Stato.Equals("Focus"))
                            {
                                item.TempoFocus += (float)(DateTime.Now - lastUpdate).TotalMilliseconds;
                                lastUpdate = DateTime.Now;
                            }

                            // Calcola la percentuale
                            item.PercentualeFocus = (item.TempoFocus / (float)(DateTime.Now - connectionTime).TotalMilliseconds * 100).ToString("n2");
                        }
                        listView1Mutex.ReleaseMutex();
                    }
                    
                    // Delegato necessario per poter aggiornare la listView, dato che operazioni come Refresh() possono essere chiamate
                    // solo dal thread proprietario, che è quello principale e non quello che esegue manageStatistics()
                    listView1.Dispatcher.Invoke(delegate
                    {
                        listView1.Items.Refresh();
                    });

                    // Aggiorna le statistiche ogni mezzo secondo
                    Thread.Sleep(500);
                }
            }
            catch (ThreadInterruptedException exception)
            {
                // TODO: c'è qualcosa da fare?
                // TODO: check se il thread muore davvero
                return;
            }
        }

        /* Viene eseguito in un thread a parte.
         * Si occupa della ricezione e gestione delle notifiche.
         */
        private void manageNotifications(Socket sock)
        {
            try
            {
                byte[] buf = new byte[1024];
                StringBuilder completeMessage = new StringBuilder();
                NetworkStream networkStream = new NetworkStream(sock);

                // Vecchia condizione: !((sock.Poll(1000, SelectMode.SelectRead) && (sock.Available == 0)) || !sock.Connected
                // Poll() ritorna true se la connessione è chiusa, resettata, terminata o in attesa (non attiva), oppure se è attiva e ci sono dati da leggere
                // Available() ritorna il numero di dati da leggere
                // Se Available() è 0 e Poll() ritorna true, la connessione è chiusa

                // TODO: Da vedere se CanRead è la scelta migliore per il NetworkStream. Giunto all'uso di NetworkStream dopo diverse soluzioni per 
                //       la gestione singola dei byte in arrivo. In questa configurazione il riempimento della lista è troppo lento, specialmente dopo una riconnessione.
                //       Probabilmente più efficiente tornare alla precedente Socket.Connected che verifica se il socket è connesso o meno
                while (networkStream.CanRead && sock.Connected)
                {
                    int i = 0;
                    int progNameLength = 0;
                    string progName = null;
                    string operation = null;
                    StringBuilder sb = new StringBuilder();
                    Array.Clear(buf, 0, buf.Length);

                    try
                    {
                        /* Leggi e salva il tipo di operazione */
                        char c;
                        sb.Append( (char) networkStream.ReadByte() );
                        sb.Append( (char) networkStream.ReadByte() );
                        do
                        {
                            c = (char) networkStream.ReadByte();
                            sb.Append(c);

                        } while (c != '-');
                        operation = sb.ToString(); 
                        sb.Clear();

                        /* Leggi e salva lunghezza nome programma */
                        i = 0;                       
                        do
                        {
                            c = (char)networkStream.ReadByte();
                            buf[i++] = (byte) c;
                        }
                        while (networkStream.DataAvailable && c != '-');
                        progNameLength = Int32.Parse(Encoding.ASCII.GetString(buf, 0, i-1));

                        /* Leggi e salva nome programma */
                        byte[] buffer = new byte[progNameLength];
                        networkStream.Read(buffer, 0, progNameLength);
                        progName = Encoding.ASCII.GetString(buffer, 0, progNameLength);
                    }
                    catch (Exception e)
                    {
                        var excString = e.ToString();
                        //TODO: Reagire
                    }

                    /* Possibili valori ricevuti:
                        * --FOCUS-<lunghezza_nome>-<nome_nuova_app_focus>
                        * --CLOSE-<lunghezza_nome>-<nome_app_chiusa>
                        * --OPENP-<lunghezza_nome>-<nome_nuova_app_aperta>-<dimensione_bitmap>-<bitmap>
                        */                        

                    switch (operation)
                    {
                        case "--FOCUS-":
                            if (listView1Mutex.WaitOne(1000))
                            {
                                // Cambia programma col focus
                                foreach (ListViewRow item in listView1.Items)
                                {
                                    if (item.Nome.Equals(progName))
                                    {
                                        item.Stato = "Focus";
                                    }
                                    else if (item.Stato.Equals("Focus"))
                                        item.Stato = "Background";
                                }                                        
                                listView1Mutex.ReleaseMutex();
                            }
                            break;
                        case "--CLOSE-":
                            // Rimuovi programma dalla listView
                            foreach (ListViewRow item in listView1.Items)
                            {
                                if (item.Nome.Equals(progName))
                                {
                                    listView1.Dispatcher.Invoke(delegate
                                    {
                                        if (listView1Mutex.WaitOne(1000))
                                        {
                                            listView1.Items.Remove(item);
                                                // Alternativa senza cancellare la riga: item.Stato = "Terminata";
                                                // Ma problemi quando ritorna in focus un programma con lo stesso nome,
                                                // visto che a volte viene passato a "Focus" anche lo stato della vecchia istanza
                                            listView1Mutex.ReleaseMutex();
                                        }
                                    });
                                    break;
                                }
                            }                            
                            break;

                        case "--OPENP-":
                            /* Ricevi icona processo */
                            Bitmap bitmap = new Bitmap(64, 64);
                            bitmap.MakeTransparent();               // <-- TODO: Tentativo veloce di togliere lo sfondo nero all'icona
                            Array.Clear(buf, 0, buf.Length);
                            try
                            {
                                /* Leggi e salva la lunghezza dell'icona leggendo i successivi byte fino a '-' (escludendo il primo) */
                                i = 0;
                                char c = (char)networkStream.ReadByte(); // leggi il primo trattino
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
                                    received += networkStream.Read(bmpData, received, bmpLength-received);
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

                                listView1.Dispatcher.Invoke(delegate
                                {
                                    addItemToListView(progName, bmpImage);
                                });

                                
                            }
                            catch(Exception e)
                            {
                                // TODO: Gestisci eccezione
                                break;
                            }

                            break;
                    }

                }
            }
            catch (ThreadInterruptedException exception)
            {
                // TODO: c'è qualcosa da fare?
                // TODO: check che il thread muoia davvero
                return;
            }
        }

        public Bitmap CopyDataToBitmap(byte[] data)
        {
            //Here create the Bitmap to the know height, width and format
            Bitmap bmp = new Bitmap(256, 256, System.Drawing.Imaging.PixelFormat.Format32bppRgb);

            //Create a BitmapData and Lock all pixels to be written 
            BitmapData bmpData = bmp.LockBits(
                                 new System.Drawing.Rectangle(0, 0, bmp.Width, bmp.Height),
                                 ImageLockMode.WriteOnly, bmp.PixelFormat);

            //Copy the data from the byte array into BitmapData.Scan0
            Marshal.Copy(data, 0, bmpData.Scan0, data.Length);


            //Unlock the pixels
            bmp.UnlockBits(bmpData);


            //Return the bitmap 
            return bmp;
        }

        private void buttonDisconentti_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                // Disabilita e chiudi socket
                sock.Shutdown(SocketShutdown.Both);
                sock.Close();

                // Aggiorna bottoni
                buttonDisconnetti.Visibility = Visibility.Hidden;
                buttonConnetti.Visibility = Visibility.Visible;
                buttonInvia.IsEnabled = false;
                textBoxIpAddress.IsEnabled = true;
                buttonCattura.IsEnabled = false;

                // Aggiorna stato
                textBoxStato.AppendText("\nSTATO: Disconnesso.");
                textBoxStato.ScrollToEnd();

                // Uccidi thread per statistiche e notifiche
                // Sconsigliato uso di Abort(), meglio Interrupt e gestione relativa eccezione nel thread
                statisticsThread.Interrupt();
                notificationsThread.Interrupt();

                // Svuota listView
                if (listView1Mutex.WaitOne(1000))
                {
                    listView1.Items.Clear();
                    listView1Mutex.ReleaseMutex();
                }

                // Ripristina cattura comando
                textBoxComando.Text = "";
                buttonCattura.Visibility = Visibility.Visible;
                buttonAnnullaCattura.Visibility = Visibility.Hidden;
                comandoDaInviare.Clear();
            }
            catch (Exception exc) { System.Windows.MessageBox.Show(exc.ToString()); }
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
                int NumDiBytesInviati = sock.Send(messaggio);

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

        private void textBoxIpAddress_TextChanged(object sender, TextChangedEventArgs e)
        {

        }

        private void listView_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {

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
    }
}
