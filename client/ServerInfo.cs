using System.ComponentModel;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Net;
using System.Net.Sockets;

namespace client
{
    class ServerInfo
    {       
        public string serverName { get; set; }
        public TcpClient server { get; set; }
        public MyTable table { get; set; }
        public bool isOnline { get; set; }
        public BackgroundWorker statisticsBw { get; set; }
        public BackgroundWorker notificationsBw { get; set; }
        
        public ServerInfo(string serverName, TcpClient server, bool isOnline)
        {
            this.serverName = serverName;
            this.server = server;
            this.isOnline = isOnline;
            table = new MyTable();
        }

        public ServerInfo()
        {

        }

        ~ServerInfo()
        {
            
        }
    }
}
