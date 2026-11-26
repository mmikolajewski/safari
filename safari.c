#include <mpi.h>
#include <stddef.h>
#include <stdio.h>

#define TAG_ZGLOSZENIE 1
#define TAG_START 2
#define ROZMIAR_GRUPY 2

typedef enum {
    ORGANIZATOR,
    KIBIC,
    UCZESTNIK
} Rola;

typedef enum {
    UCZ_NORMALNY,
    UCZ_UBIEGA_SIE_O_WYCIECZKE,
    UCZ_UCZESTNICZY_W_WYCIECZCE,
    UCZ_DOSTAJE_WPIERDOL,
    UCZ_KOSTNICA,
    UCZ_SZPITAL
} StanUczestnika;

typedef enum {
    ORG_ZBIERA_ZALOGE,
    ORG_PROWADZI_WYCIECZKE,
    ORG_DOSTAJE_PO_RYJU,
    ORG_KONIEC_WYCIECZKI,
    ORG_PRZEBYWA_W_SZPITALU
} StanOrganizatora;

typedef enum {
    KIB_NORMALNY,
    KIB_SZUKA_ZACZEPKI,
    KIB_BIJE_SIE,
    KIB_UCIEKA,
    KIB_UKRYWA_SIE
} StanKibica;

StanUczestnika stan_uczestnika = UCZ_NORMALNY;
StanOrganizatora stan_organizatora = ORG_ZBIERA_ZALOGE;
StanKibica stan_kibica = KIB_NORMALNY;

int liczba_zebranych = 0;
int rozmiar_grupy = ROZMIAR_GRUPY;
int zaloga[ROZMIAR_GRUPY];

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int pid;
    MPI_Comm_rank(MPI_COMM_WORLD, &pid);

    Rola rola;

    if (pid == 0) {
        rola = ORGANIZATOR;
    } else if (pid == 1) {
        rola = KIBIC;
    } else {
        rola = UCZESTNIK;
    }

    while (1) {
        switch (rola) {
            case ORGANIZATOR:
                switch (stan_organizatora) {
                    case ORG_ZBIERA_ZALOGE: {
                        int jest_zgloszenie;
                        MPI_Status status;

                        // Sprawdzam, czy ktoś zgłosił się na wycieczkę.
                        MPI_Iprobe(MPI_ANY_SOURCE, TAG_ZGLOSZENIE,
                                   MPI_COMM_WORLD, &jest_zgloszenie, &status);

                        if (jest_zgloszenie) {
                            MPI_Recv(NULL, 0, MPI_INT, status.MPI_SOURCE,
                                     TAG_ZGLOSZENIE, MPI_COMM_WORLD,
                                     MPI_STATUS_IGNORE);

                            // Zapisuję numer uczestnika i zwiększam licznik.
                            zaloga[liczba_zebranych] = status.MPI_SOURCE;
                            liczba_zebranych++;

                            // Tymczasowy wydruk do sprawdzenia licznika.
                            printf("Organizator: mam %d/%d uczestnikow\n",
                                   liczba_zebranych, rozmiar_grupy);
                            fflush(stdout);
                        }

                        if (liczba_zebranych == rozmiar_grupy) {
                            // Mamy komplet. Każdy z załogi dostaje start.
                            for (int i = 0; i < liczba_zebranych; i++) {
                                MPI_Send(NULL, 0, MPI_INT, zaloga[i],
                                         TAG_START, MPI_COMM_WORLD);
                            }

                            stan_organizatora = ORG_PROWADZI_WYCIECZKE;
                        }

                        break;
                    }

                    // TUTAJ później dopiszemy ORG_PROWADZI_WYCIECZKE.
                    default:
                        break;
                }
                break;

            case KIBIC:
                // TUTAJ później dodamy switch (stan_kibica).
                break;

            case UCZESTNIK:
                switch (stan_uczestnika) {
                    case UCZ_NORMALNY:
                        // Wysyłam zgłoszenie do organizatora, czyli procesu 0.
                        MPI_Send(NULL, 0, MPI_INT, 0,
                                 TAG_ZGLOSZENIE, MPI_COMM_WORLD);

                        stan_uczestnika = UCZ_UBIEGA_SIE_O_WYCIECZKE;
                        break;

                    case UCZ_UBIEGA_SIE_O_WYCIECZKE: {
                        int jest_start;

                        // Sprawdzam, czy organizator wysłał start.
                        MPI_Iprobe(0, TAG_START, MPI_COMM_WORLD,
                                   &jest_start, MPI_STATUS_IGNORE);

                        if (jest_start) {
                            MPI_Recv(NULL, 0, MPI_INT, 0, TAG_START,
                                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                            stan_uczestnika = UCZ_UCZESTNICZY_W_WYCIECZCE;
                        }

                        break;
                    }

                    // TUTAJ później dopiszemy UCZ_UCZESTNICZY_W_WYCIECZCE.
                    default:
                        break;
                }
                break;
        }
    }
}