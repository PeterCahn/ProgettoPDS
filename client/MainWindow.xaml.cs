using System;
using System.Collections.Generic;
using System.Linq;
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
    public partial class MainWindow : Window
    {

        byte[] buffer = new byte[1024];
        Socket sock;

        public MainWindow()
        {
            InitializeComponent();

            // Inizialmente nascondi bottoni inutilizzabili senza connessione
            buttonDisconnetti.IsEnabled = false;
            buttonComando.IsEnabled = false;
        }

        private void buttonConnetti_Click(object sender, RoutedEventArgs e)
        {

        }

        private void buttonDisconentti_Click(object sender, RoutedEventArgs e)
        {

        }
    }
}
