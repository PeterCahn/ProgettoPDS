using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Media.Imaging;
using System.Windows.Forms;
using System.Drawing;
using System.Data;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Threading;
using System.Collections.Specialized;

namespace client
{
    class MyTable : AsyncObservableCollection<Finestra>
    {
        private ObservableCollection<Finestra> _finestre = new AsyncObservableCollection<Finestra>();
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

        public void addFinestra(Int32 hwnd, string nomeFinestra, string statoFinestra, double tempoFocusPerc, double tempoFocus, BitmapImage icona)
        {
            _finestre.Add(new Finestra(hwnd, nomeFinestra, statoFinestra, tempoFocusPerc, tempoFocus, icona));
        }

        public void changeFocus(Int32 hwnd, string statoFinestra)
        {
            var it = _finestre.GetEnumerator();
            
        }

        public void removeWnd(Int32 hwnd)
        {

        }        
        
    }

    class Finestra : INotifyPropertyChanged
    {
        private Int32 _hwnd { get; set; }
        private string _nomeFinestra { get; set; }
        private string _statoFinestra { get; set; }
        private double _tempoFocusPerc { get; set; }
        private double _tempoFocus { get; set; }
        private BitmapImage _icona { get; set; }

        public Int32 Hwnd
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
