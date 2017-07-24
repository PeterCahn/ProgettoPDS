using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Net;

namespace client
{
    class Server
    {
        public string Name { get; set; }
        public IPAddress Address { get; set; }

        override public string ToString()
        {
            if (Address != null)
                return Address.ToString();
            else
                return Name;
        }
    }
}
