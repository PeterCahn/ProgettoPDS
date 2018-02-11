using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Net;
using System.Threading;
using System.Net.Sockets;

namespace client
{
    class ServerInfo
    {       
        public TcpClient server { get; set; }
        public MyTable table { get; set; }
        public Thread statisticTread { get; set; }
        public Thread notificationsTread { get; set; }
        public ManualResetEvent disconnectionEvent { get; set; } 
        public Mutex tableModificationsMutex { get; set; }
        
    }
}
