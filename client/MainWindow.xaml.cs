﻿using System;
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

namespace WpfApplication1
{

    class ListViewRow
    {
        public string Icona { get; set; }
        public string Nome { get; set; }
        public string Stato { get; set; }
        public float TempoFocus { get; set; }
    }

    public partial class MainWindow : Window
    {

        byte[] buffer = new byte[1024];
        Socket sock;

        public MainWindow()
        {
            InitializeComponent();

            // Inizialmente imposta bottoni come inutilizzabili senza connessione
            buttonDisconnetti.Visibility = Visibility.Hidden;
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

                // Ricevi nomi applicazioni
                byte[] buf = new byte[1024];
                int dim = sock.Receive(buf);
                string stringRicevuta = Encoding.ASCII.GetString(buf, 0, dim);
                while (!stringRicevuta.Equals("--END"))
                {                   
                    addItemToListview(stringRicevuta);
                    dim = sock.Receive(buf);
                    stringRicevuta = Encoding.ASCII.GetString(buf, 0, dim);
                }

                // Ricevi app che ha il focus
                dim = sock.Receive(buf);
                stringRicevuta = Encoding.ASCII.GetString(buf, 0, dim);
                for (int i = 0; i < listView.Items.Count; i++)
                {
                    if (((ListViewRow)listView.Items[i]).Nome == stringRicevuta)
                    {
                        ((ListViewRow)listView.Items[i]).Stato = "Focus";
                    }
                }
            }
            catch (Exception exc) {
                textBoxStato.AppendText("\nECCEZIONE: " + exc.ToString());
                textBoxStato.ScrollToEnd();
            }
        }

        /* TODO: Percentuale "Live" */
        private void addItemToListview(string appName)
        {            
            // TODO: Tutto momentaneamente farlocco tranne nome
            listView.Items.Add(new ListViewRow() { Icona = "", Nome = appName, Stato = "Background", TempoFocus = 100 });
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

                // Aggiorna stato
                textBoxStato.AppendText("\nSTATO: Disconnesso.");
                textBoxStato.ScrollToEnd();
            }
            catch (Exception exc) { MessageBox.Show(exc.ToString()); }
        }

        private void buttonInvia_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                // Invia messaggio 
                string messaggio = textBoxComando.Text;
                byte[] msg = Encoding.Unicode.GetBytes(messaggio + "<END>");
                int NumDiBytesInviati = sock.Send(msg);

                /* TODO: ricevi */

                // Aggiorna bottoni e textBox
                textBoxComando.Text = "";
                buttonInvia.IsEnabled = false;
                buttonCattura.Content = "Cattura Comando";
                buttonCattura.IsEnabled = true;

                // Rimuovi event handler per non scrivere più i bottoni premuti nel textBox
                this.KeyDown -= new KeyEventHandler(OnButtonKeyDown);

            }
            catch (Exception exc) {
                textBoxStato.AppendText("\nECCEZIONE: " + exc.ToString());
                textBoxStato.ScrollToEnd();
            }
        }

        private void buttonCattura_Click(object sender, RoutedEventArgs e)
        {

            buttonCattura.Content = "In cattura...";
            buttonCattura.IsEnabled = false;

            // Crea event handler per scrivere i tasti premuti
            this.KeyDown += new KeyEventHandler(OnButtonKeyDown);
        }

        private void OnButtonKeyDown(object sender, KeyEventArgs e)
        {
            if (textBoxComando.Text.Length == 0) {
                textBoxComando.Text = e.Key.ToString();
                buttonInvia.IsEnabled = true;
            }
            else {
                if (!textBoxComando.Text.Contains(e.Key.ToString()))
                    textBoxComando.AppendText("+" + e.Key.ToString());
            }
        }

        private void textBoxIpAddress_TextChanged(object sender, TextChangedEventArgs e)
        {

        }

        private void listView_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {

        }
    }
}
