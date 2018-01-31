/* TODO:
	- Questione lista applicazioni ed app multithread: a Jure hanno detto che avrebbe dovuto mostrare i thread
	- Il reinterpret_cast è corretto? Cioè, è giusto usarlo dov'è usato?
	- Cos'è la finestra "Program Manager"?
	- Gestione finestra senza nome (Desktop)
	- Deallocazione risorse
	- Quando il client si chiude o si chiude anche il server (e non dovrebbe farlo) o se sopravvive alla prossima apertura di un client non funziona bene
	  perchè avvia un nuovo thread notificationsThread senza uccidere il precedente
	- Se due finestre hanno lo stesso nome ed una delle due è in focus, vengono entrambe segnate come in focus perchè non sa distinguere quale lo sia veramente, ma poi
	  solo la prima nella lista del client ha la percentuale che aumenta
	- Gestire caso in cui finestra cambia nome (es: "Telegram" con 1 notifica diventa "Telegram(1)")
*/

#include "Server.h"

int main(int argc, char* argv[])
{
	Server server = Server();
	server.start();

	
	return 0;
}

