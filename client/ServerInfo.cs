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

        /*
        public ServerInfo(string serverName, TcpClient server, bool isOnline)
        {
            this.serverName = serverName;
            this.server = server;
            this.table = new MyTable();
            disconnectionEvent = new ManualResetEvent(false);
            forcedDisconnectionEvent = new AutoResetEvent(false);
            tableModificationsMutex = new Mutex();
            this.isOnline = isOnline;
        }
        */

        public ServerInfo()
        {
            serverName = "";
        }

        ~ServerInfo()
        {            
            disconnectionEvent.Close();
            disconnectionEvent.Dispose();

            forcedDisconnectionEvent.Close();
            forcedDisconnectionEvent.Dispose();

            tableModificationsMutex.Close();
            tableModificationsMutex.Dispose();

            statisticThread.Join();
            notificationsThread.Join();
        }
    }
}
