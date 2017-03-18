using System;
using System.Collections.Generic;
using System.Linq;
using System.Data;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using System.Threading;


namespace WpfApplication1
{
    class ListViewRow 
    {
        public string Icona { get; set; }
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

                /* // Interrogazione DNS, crea IPHostEntry contenente vettore di IPAddress di risultato          
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

                // Ricevi nomi applicazioni ed aggiungile alla lista
                byte[] buf = new byte[1024];
                int dim = sock.Receive(buf);
                string stringRicevuta = Encoding.ASCII.GetString(buf, 0, dim);
                while (!stringRicevuta.Equals("--END"))
                {
                    addItemToListView(stringRicevuta);
                    dim = sock.Receive(buf);
                    stringRicevuta = Encoding.ASCII.GetString(buf, 0, dim);
                }

                // Ricevi app che ha il focus
                getFocus(sock);

                // Avvia thread per statistiche live
                statisticsThread = new Thread(new ThreadStart(this.manageStatistics));
                statisticsThread.IsBackground = true;
                statisticsThread.Start();

                // Avvia thread per eventuali notifiche
                notificationsThread = new Thread(() => manageNotifications(sock));
                notificationsThread.IsBackground = true;
                notificationsThread.Start();
            }
            catch (Exception exc)
            {
                textBoxStato.AppendText("\nECCEZIONE: " + exc.ToString());
                textBoxStato.ScrollToEnd();
            }
        }

        /* TODO: aggiungi anche icona */
        void addItemToListView(string nomeProgramma)
        {
            listView1.Items.Add(new ListViewRow() { Icona = "", Nome = nomeProgramma, Stato = "Background", PercentualeFocus = "0", TempoFocus = 0 });
        }

        /* Riceve ed imposta applicazione col focus */
        private void getFocus(Socket sock)
        {
            byte[] buf = new byte[1024];
            int dim = sock.Receive(buf);
            string stringRicevuta = Encoding.ASCII.GetString(buf, 0, dim);
            foreach (ListViewRow item in listView1.Items)
            {
                if (item.Nome.Equals(stringRicevuta))
                {
                    item.Stato = "Focus";
                    break;
                }
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
                while (true)
                {
                    // Sleep() necessario per evitare divisione per 0 alla prima iterazione e mostrare NaN per il primo mezzo secondo nelle statistiche
                    Thread.Sleep(1);
                    foreach (ListViewRow item in listView1.Items)
                    {
                        if (item.Stato.Equals("Focus"))
                        {
                            item.TempoFocus += (float)(DateTime.Now - lastUpdate).TotalMilliseconds;
                            lastUpdate = DateTime.Now;
                        }

                        // Calcola la percentuale
                        item.PercentualeFocus = (item.TempoFocus / (float)(DateTime.Now - connectionTime).TotalMilliseconds * 100).ToString("n2");
                        
                        // Delegato necessario per poter aggiornare la listView, dato che operazioni come Refresh() possono essere chiamate
                        // solo dal thread proprietario, che è quello principale e non quello che esegue manageStatistics()
                        listView1.Dispatcher.Invoke(delegate
                        {
                            listView1.Items.Refresh();
                        });
                    }
                    // Aggiorna le statistiche ogni mezzo secondo
                    Thread.Sleep(500);
                }
            }
            catch (ThreadInterruptedException exception)
            {
                // TODO: c'è qualcosa da fare?
                // TODO: check che il thread muore davvero
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
                int dim = -1;
                string stringRicevuta;
                byte[] buf = new byte[1024];

                // Vecchia condizione: !((sock.Poll(1000, SelectMode.SelectRead) && (sock.Available == 0)) || !sock.Connected
                // Poll() ritorna true se la connessione è chiusa, resettata, terminata o in attesa (non attiva), oppure se è attiva e ci sono dati da leggere
                // Available() ritorna il numero di dati da leggere
                // Se Available() è 0 e Poll() ritorna true, la connessione è chiusa

                // Connected() ritorna false se il socket non è inizializzato
                while (sock.Connected)
                {
                    try
                    {
                        dim = sock.Receive(buf);
                    }
                    catch (SocketException se)
                    {
                        // TODO: fai qualcosa
                    }
                    if (dim != -1)
                    {
                        stringRicevuta = Encoding.ASCII.GetString(buf, 0, dim);
                        // Possibili valori ricevuti:
                        // --FOCUS-<nome_nuova_app_focus>
                        // --CLOSE-<nome_app_chiusa>
                        // --OPENP-<nome_nuova_app_aperta>
                        String operation = stringRicevuta.Substring(0, 8);
                        String progName = stringRicevuta.Substring(8, stringRicevuta.Length - 8);
                        switch (operation)
                        {
                            case "--FOCUS-":
                                // Cambia programma col focus
                                foreach (ListViewRow item in listView1.Items)
                                {
                                    if (item.Nome.Equals(progName)) {
                                        item.Stato = "Focus";
                                    }
                                    else if (item.Stato.Equals("Focus"))
                                        item.Stato = "Background";
                                }
                                break;
                            case "--CLOSE-":
                                // Rimuovi programma dalla listView
                                foreach (ListViewRow item in listView1.Items)
                                {
                                    if (item.Nome.Equals(progName))
                                    {
                                        listView1.Dispatcher.Invoke(delegate {
                                            listView1.Items.Remove(item); 
                                            // Alternativa senza cancellare la riga: item.Stato = "Terminata";
                                            // Ma problemi quando ritorna in focus un programma con lo stesso nome,
                                            // visto che a volte viene passato a "Focus" anche lo stato della vecchia istanza
                                        });
                                        break;
                                    }
                                }
                                break;
                            case "--OPENP-":
                                listView1.Dispatcher.Invoke(delegate
                                {
                                    addItemToListView(progName);
                                });
                                break;
                        }
                    }
                }
            }
            catch (ThreadInterruptedException exception)
            {
                // TODO: c'è qualcosa da fare?
                // TODO: check che il thread muore davvero
                return;
            }
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
                listView1.Items.Clear();

                // Ripristina cattura comando
                textBoxComando.Text = "";
                buttonCattura.Visibility = Visibility.Visible;
                buttonAnnullaCattura.Visibility = Visibility.Hidden;
                comandoDaInviare.Clear();
            }
            catch (Exception exc) { MessageBox.Show(exc.ToString()); }
        }

        private void buttonInvia_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                byte[] messaggio;

                // Serializza messaggio da inviare
                System.Text.StringBuilder sb = new System.Text.StringBuilder();
                foreach (int virtualKey in comandoDaInviare) {
                    if (sb.Length != 0)
                        sb.Append("+");
                    sb.Append(virtualKey.ToString());
                    }
                sb.Append("\0");
                messaggio = Encoding.ASCII.GetBytes(sb.ToString());

                // Invia messaggio
                int NumDiBytesInviati = sock.Send(messaggio);

                /* TODO: ricevi */

                // Aggiorna bottoni e textBox
                textBoxComando.Text = "";
                buttonInvia.IsEnabled = false;
                buttonCattura.IsEnabled = true;
                buttonCattura.Visibility = Visibility.Visible;
                buttonAnnullaCattura.Visibility = Visibility.Hidden;

                // Svuota lista di tasti premuti da inviare
                comandoDaInviare.Clear();

                // Rimuovi event handler per non scrivere più i bottoni premuti nel textBox
                this.KeyDown -= new KeyEventHandler(OnButtonKeyDown);

            }
            catch (Exception exc)
            {
                textBoxStato.AppendText("\nECCEZIONE: " + exc.ToString());
                textBoxStato.ScrollToEnd();
            }
        }

        private void buttonCattura_Click(object sender, RoutedEventArgs e)
        {
            buttonCattura.IsEnabled = false;
            buttonCattura.Visibility = Visibility.Hidden;
            buttonAnnullaCattura.Visibility = Visibility.Visible;

            // Crea event handler per scrivere i tasti premuti
            this.KeyDown += new KeyEventHandler(OnButtonKeyDown);
        }

        private void OnButtonKeyDown(object sender, KeyEventArgs e)
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
