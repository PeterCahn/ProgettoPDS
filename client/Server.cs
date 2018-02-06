using System.Net;

namespace client
{
    class Server
    {
        public string Name { get; set; }
        public IPAddress Address { get; set; }

        override public string ToString()
        {
            if (Name != null)
                return Name;
            else
                return Address.ToString();
        }
        /*
        public bool CheckValidity()
        {
            return IPAddress.TryParse(Address.ToString(), Address);
            
        }
        */
    }
}
