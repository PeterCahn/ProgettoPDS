using System;
using System.Collections.Generic;
using System.Windows.Media.Imaging;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Threading;
using System.Collections.Specialized;

using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Drawing;
using System.Data;

namespace client
{
    class MyTable : AsyncObservableCollection<Finestra>
    {
        private ObservableCollection<Finestra> _finestre;
        public AsyncObservableCollection<Finestra> Finestre {
            get { return (AsyncObservableCollection<Finestra>) _finestre; }
            set {
                if(_finestre != value)
                {
                    _finestre = value;
                    OnPropertyChanged(new PropertyChangedEventArgs("Finestre"));
                }
            }
        }
        private DateTime connectionTime;
        private DateTime lastUpdate { get; set; }

        public MyTable()
        {
            _finestre = new AsyncObservableCollection<Finestra>();

            // Aggiungi finestra nascosta che racchiude le statistiche di tutto ciò che non è una finestra sul server
            addFinestra(0, "", "Background", 0, 0, null);

            // Rendi la finestra non visibile
            Finestre.First().Visible = false;

            // Inizializza i tempi per l'aggiornamento delle statistiche
            connectionTime = DateTime.Now;
            lastUpdate = connectionTime;
        }

        ~MyTable()
        {
            //System.Windows.MessageBox.Show("Distruttore di MyTable chiamato");            
        }

        public void addFinestra(int hwnd, string nomeFinestra, string statoFinestra, double tempoFocusPerc, double tempoFocus, BitmapImage icona)
        {
            lock (this)
            {
                Finestre.Add(new Finestra(hwnd, nomeFinestra, statoFinestra, tempoFocusPerc, tempoFocus, icona));
            }            
        }

        public void changeFocus(int hwnd)
        {
            lock (this)
            {
                bool trovato = false;
                int indexOfFocus = -1;
                foreach (Finestra finestra in Finestre)
                {
                    if (finestra.Hwnd.Equals(hwnd))
                    {
                        finestra.StatoFinestra = "Focus";
                        indexOfFocus = Finestre.IndexOf(finestra);
                        trovato = true;
                    }
                    else if (finestra.StatoFinestra.Equals("Focus"))
                        finestra.StatoFinestra = "Background";
                }

                if (!trovato)
                {
                    // First() perché il primo elemento sarà sempre la finestra che raccoglie le statistiche di quando niente è in focus.
                    // Questa finestra non verrà mai eliminata durante l'esecuzione, quindi sarà sempre la prima.
                    Finestre.First().StatoFinestra = "Focus";
                }
                else
                {
                    // Mette in cima la finestra che è in focus
                    Finestre.Move(indexOfFocus, 0);

                    // Ordina le finestre in base al tempo in cui sono state in focus.
                    // Chiamata qui in modo che venga chimata meno spesso 
                    // rispetto a ogni mezzo secondo di aggiornamento statistiche del tempo in focus
                    Finestre.OrderBy(f => f.TempoFocus);
                }

            }
        }

        public void removeFinestra(int hwnd)
        {
            lock (this)
            {
                foreach (Finestra finestra in Finestre)
                    if (finestra.Hwnd.Equals(hwnd))
                    {
                        Finestre.Remove(finestra);
                        break;
                    }
            }
        }
           
        public void cambiaTitoloFinestra(int hwnd, string nomeFinestra)
        {
            lock (this)
            {
                foreach (Finestra finestra in Finestre)
                {
                    if (finestra.Hwnd.Equals(hwnd))
                    {
                        finestra.NomeFinestra = nomeFinestra;                        
                        break;
                    }
                }                
            }
        }

        public void aggiornaStatisticheFocus()
        {
            lock (this)
            {
                foreach (Finestra finestra in Finestre)
                {
                    /* Incrementa il TempoFocus della finestra con StatoFinestra in Focus */
                    if (finestra.StatoFinestra.Equals("Focus"))
                    {
                        finestra.TempoFocus += (DateTime.Now - lastUpdate).TotalMilliseconds;
                        lastUpdate = DateTime.Now;
                    }                    
                    
                    // Calcola la percentuale
                    double perc = (finestra.TempoFocus / (DateTime.Now - connectionTime).TotalMilliseconds) * 100;
                    finestra.TempoFocusPerc = Math.Round(perc, 2); // arrotonda la percentuale mostrata a due cifre dopo la virgola
                    
                }
            }
        }        

    }

    class Finestra : INotifyPropertyChanged
    {
        private int _hwnd { get; set; }
        private string _nomeFinestra { get; set; }
        private string _statoFinestra { get; set; }
        private double _tempoFocusPerc { get; set; }
        private double _tempoFocus { get; set; }
        private BitmapImage _icona { get; set; }
        private bool _visible { get; set; }

        public int Hwnd
        {
            get { return _hwnd; }
            set
            {
                if (_hwnd != value)
                {
                    _hwnd = value;
                    OnPropertyChanged(this, "Hwnd");
                }                    
            }
        }
        public string NomeFinestra
        {
            get { return _nomeFinestra; }
            set
            {
                if (_nomeFinestra != value)
                {
                    _nomeFinestra = value;
                    OnPropertyChanged(this, "NomeFinestra");
                }
            }
        }
        public string StatoFinestra
        {
            get { return _statoFinestra; }
            set
            {
                if (_statoFinestra != value)
                {
                    _statoFinestra = value;
                    OnPropertyChanged(this, "StatoFinestra");
                }
            }
        }
        public double TempoFocusPerc
        {
            get { return _tempoFocusPerc; }
            set
            {
                if (_tempoFocusPerc != value)
                {
                    _tempoFocusPerc = value;
                    OnPropertyChanged(this, "TempoFocusPerc");
                }
            }
        }
        public double TempoFocus
        {
            get { return _tempoFocus; }
            set
            {
                if (_tempoFocus != value)
                {
                    _tempoFocus = value;
                    OnPropertyChanged(this, "TempoFocus");
                }
            }
        }
        public BitmapImage Icona
        {
            get { return _icona; }
            set
            {
                if (_icona != value)
                {
                    _icona = value;
                    OnPropertyChanged(this, "Icona");
                }
            }
        }

        public bool Visible
        {
            get { return _visible; }
            set
            {
                if (_visible != value)
                {
                    _visible = value;
                    OnPropertyChanged(this, "Visible");
                }
                
            }
        }

        public event PropertyChangedEventHandler PropertyChanged;

        // OnPropertyChanged will raise the PropertyChanged event passing the
        // source property that is being updated.
        private void OnPropertyChanged(object sender, string propertyName)
        {
            if (this.PropertyChanged != null)
            {
                PropertyChanged(sender, new PropertyChangedEventArgs(propertyName));
            }
        }

        public Finestra(Int32 hwnd, string nomeFinestra, string statoFinestra, double tempoFocusPerc, double tempoFocus, BitmapImage icona)
        {
            Hwnd = hwnd;
            NomeFinestra = nomeFinestra;
            StatoFinestra = statoFinestra;
            TempoFocusPerc = tempoFocusPerc;
            TempoFocus = tempoFocus;
            Icona = icona;
            Visible = true;
        }
    }

    public class AsyncObservableCollection<T> : ObservableCollection<T>
    {
        private SynchronizationContext _synchronizationContext = SynchronizationContext.Current;

        public AsyncObservableCollection()
        {
        }

        public AsyncObservableCollection(IEnumerable<T> list)
            : base(list)
        {
        }

        protected override void OnCollectionChanged(NotifyCollectionChangedEventArgs e)
        {
            if (SynchronizationContext.Current == _synchronizationContext)
            {
                // Execute the CollectionChanged event on the current thread
                RaiseCollectionChanged(e);
            }
            else
            {
                // Raises the CollectionChanged event on the creator thread
                _synchronizationContext.Send(RaiseCollectionChanged, e);
            }
        }

        private void RaiseCollectionChanged(object param)
        {
            // We are in the creator thread, call the base implementation directly
            base.OnCollectionChanged((NotifyCollectionChangedEventArgs)param);
        }

        protected override void OnPropertyChanged(PropertyChangedEventArgs e)
        {
            if (SynchronizationContext.Current == _synchronizationContext)
            {
                // Execute the PropertyChanged event on the current thread
                RaisePropertyChanged(e);
            }
            else
            {
                // Raises the PropertyChanged event on the creator thread
                _synchronizationContext.Send(RaisePropertyChanged, e);
            }
        }

        private void RaisePropertyChanged(object param)
        {
            // We are in the creator thread, call the base implementation directly            
            base.OnPropertyChanged((PropertyChangedEventArgs)param);
        }
    }

}
