using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Media.Imaging;
using System.Data;

namespace client
{
    class MyTable
    {
        public DataTable rowsList { get; set; }

        public MyTable()
        {
            rowsList = new DataTable();

            DataColumn nameColumn = new DataColumn();
            nameColumn.DataType = System.Type.GetType("System.String");
            nameColumn.ColumnName = "Nome applicazione";
            nameColumn.ReadOnly = true;
            rowsList.Columns.Add(nameColumn);

            DataColumn statusColumn = new DataColumn();
            statusColumn.DataType = System.Type.GetType("System.String");
            statusColumn.ColumnName = "Stato finestra";
            statusColumn.ReadOnly = false;
            rowsList.Columns.Add(statusColumn);

            DataColumn percentualColumn = new DataColumn();
            percentualColumn.DataType = System.Type.GetType("System.Double");
            percentualColumn.ColumnName = "Tempo in focus (%)";
            percentualColumn.ReadOnly = false;
            rowsList.Columns.Add(percentualColumn);

            DataColumn timeColumn = new DataColumn();
            timeColumn.DataType = System.Type.GetType("System.Double");
            timeColumn.ColumnName = "Tempo in focus";
            timeColumn.ReadOnly = false;
            rowsList.Columns.Add(timeColumn);

            /*DataColumn iconColumn = new DataColumn();
            iconColumn.DataType = System.Type.GetType("System.Windows.Media.Imaging.BitmapImage");
            iconColumn.ColumnName = "Icona";
            iconColumn.ReadOnly = true;
            rowsList.Columns.Add(iconColumn);*/
        }
    }
}
