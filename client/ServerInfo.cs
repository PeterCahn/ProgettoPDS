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
        public string serverName { get; set; }
        public TcpClient server { get; set; }
        public MyTable table { get; set; }
        public Thread statisticThread { get; set; }
        public Thread notificationsThread { get; set; }
        public ManualResetEvent disconnectionEvent { get; set; }
        public AutoResetEvent forcedDisconnectionEvent { get; set; }
        public Mutex tableModificationsMutex { get; set; }
        public bool isOnline { get; set; }
    }
}
