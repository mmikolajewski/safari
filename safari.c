#include <mpi.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <mpi.h>
#include <errno.h>


#define TAG_ZGLOSZENIE 1
#define TAG_START 2
#define ROZMIAR_GRUPY 2
#define CZAS_Wycieczki 5.0
#define CZAS_UKRYWANIA 3.0
#define TAG_KONIEC 3
#define TAG_PYTANIE_O_ZACZEPKE 5
#define TAG_ODPOWIEDZ_NA_ZACZEPKE 6
#define TAG_START_WYCIECZKI 4
#define TAG_WYNIK_SPOTKANIA 7
#define CZAS_SZPITALA 5.0 




typedef enum {
    ORGANIZATOR,
    KIBIC,
    UCZESTNIK
} Rola;

typedef enum {
    UCZ_NORMALNY,
    UCZ_UBIEGA_SIE_O_WYCIECZKE,
    UCZ_UCZESTNICZY_W_WYCIECZCE,
    UCZ_BIJE_SIE,
    UCZ_SZPITAL
} StanUczestnika;

typedef enum {
    ORG_ZBIERA_ZALOGE,
    ORG_PROWADZI_WYCIECZKE,
    ORG_BIJE_SIE,
    ORG_KONIEC_WYCIECZKI,
    ORG_PRZEBYWA_W_SZPITALU
} StanOrganizatora;

typedef enum {
    KIB_NORMALNY,
    KIB_SZUKA_ZACZEPKI,
    KIB_BIJE_SIE,
    KIB_UKRYWA_SIE
} StanKibica;

StanUczestnika stan_uczestnika = UCZ_NORMALNY;
StanOrganizatora stan_organizatora = ORG_ZBIERA_ZALOGE;
StanKibica stan_kibica = KIB_NORMALNY;

int liczba_zebranych = 0;
int rozmiar_grupy = ROZMIAR_GRUPY;



typedef struct {
    int uczestnik;
    unsigned long long czas_zgloszenia;
} Zgloszenie;

int zaloga[ROZMIAR_GRUPY];
double czas_startu;
int moj_organizator = 0;
int kibic_spotkania = -1;


void pytajnik_zaczepnik(Rola rola, unsigned long long *zegar_lamporta) {
    if(rola != ORGANIZATOR &&  rola != UCZESTNIK) {
        return;
    }

    int jest_pytanie;
    MPI_Status status;

    MPI_Iprobe(MPI_ANY_SOURCE, TAG_PYTANIE_O_ZACZEPKE, MPI_COMM_WORLD, &jest_pytanie, &status);
    if(!jest_pytanie) {
        return;
    }

    int nadawca = status.MPI_SOURCE;
    unsigned long long czas_odebranej_zaczepki;
    MPI_Recv(&czas_odebranej_zaczepki, 1, MPI_UNSIGNED_LONG_LONG,nadawca, TAG_PYTANIE_O_ZACZEPKE, MPI_COMM_WORLD,MPI_STATUS_IGNORE);

    if (czas_odebranej_zaczepki > *zegar_lamporta) {
    *zegar_lamporta = czas_odebranej_zaczepki;
    }
    (*zegar_lamporta)++;

    int odpowiedz =
    (rola == ORGANIZATOR && stan_organizatora == ORG_PROWADZI_WYCIECZKE) ||
    (rola == UCZESTNIK && stan_uczestnika == UCZ_UCZESTNICZY_W_WYCIECZCE);

    if (odpowiedz == 1) {
    kibic_spotkania = nadawca;
    }
    (*zegar_lamporta)++;
    unsigned long long dane_odpowiedzi[2] = {
        (unsigned long long)odpowiedz,
        *zegar_lamporta
    };

    MPI_Send(dane_odpowiedzi, 2, MPI_UNSIGNED_LONG_LONG, nadawca, TAG_ODPOWIEDZ_NA_ZACZEPKE, MPI_COMM_WORLD);
}

int odczytaj_liczbe(const char *tekst, int maksimum)
{
    char *koniec;
    errno = 0;
    long liczba = strtol(tekst, &koniec, 10);

    if (errno != 0 || koniec == tekst || *koniec != '\0' ||
        liczba < 0 || liczba > maksimum) {
        return -1;
    }

    return (int)liczba;
}
/*
 * TODO: Zastąpić stałe przypisanie uczestnika do organizatora
 * rozproszonym doborem grup. Test `mpirun -np 4 ./safari 2 0`
 * pokazał problem: dwóch uczestników trafia do dwóch różnych
 * organizatorów. Każdy czeka na drugą osobę, choć razem mogliby
 * utworzyć pełną grupę.
 *
 * Uczestnik powinien zgłaszać chęć wyjazdu bez wybierania
 * organizatora z góry. Procesy muszą uzgodnić, które zgłoszenia
 * są gotowe do rozpatrzenia i w jakiej kolejności je obsłużyć.
 * Do porównania zgłoszeń można użyć pary (czas Lamporta, pid).
 *
 * Gdy jest komplet uczestników i wolny organizator, należy
 * przydzielić całą grupę do tego organizatora. Dopiero wtedy
 * uczestnicy zapisują jego numer w moj_organizator, a organizator
 * wpisuje członków grupy do zaloga i wysyła TAG_START.
 *
 * Dobór powinien zapobiegać podwójnemu przydzieleniu uczestnika
 * i głodzeniu oczekujących oraz pozwalać na równoległe wycieczki.
 * Po wdrożeniu powtórzyć test z dwoma organizatorami i dwoma
 * uczestnikami. Powinna ruszyć jedna pełna wycieczka.
 */
int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);
    int pid;
    MPI_Comm_rank(MPI_COMM_WORLD, &pid);

    int liczba_procesow;
    MPI_Comm_size(MPI_COMM_WORLD, &liczba_procesow);

    int liczba_organizatorow = -1;
    int liczba_kibicow = -1;

    if (argc == 3) {
            liczba_organizatorow = odczytaj_liczbe(argv[1], liczba_procesow);
            liczba_kibicow = odczytaj_liczbe(argv[2], liczba_procesow);
    }

    if (liczba_organizatorow < 1 || liczba_kibicow < 0 ||
        liczba_procesow - liczba_organizatorow - liczba_kibicow < ROZMIAR_GRUPY) {
        if (pid == 0) {
            fprintf(stderr, "Uzycie: ./safari <organizatorzy> <kibice>\n");
            fprintf(stderr, "Potrzeba przynajmniej %d uczestnikow.\n", ROZMIAR_GRUPY);
        }

        MPI_Finalize();
        return 1;
    }

int pierwszy_uczestnik = liczba_organizatorow + liczba_kibicow;

    Rola rola;

    if (pid < liczba_organizatorow) {
        rola = ORGANIZATOR;
    } else if (pid < pierwszy_uczestnik) {
        rola = KIBIC;
    } else {
        rola = UCZESTNIK;
        moj_organizator = (pid - pierwszy_uczestnik) % liczba_organizatorow;
    }



    MPI_Request zaczepka_kibica = MPI_REQUEST_NULL;
    unsigned long long czas_wyslanej_zaczepki;
    int adresat_zaczepki = -1;
    double poczatek_ukrywania = 0.0;
    double poczatek_szpitala = 0.0;
    int odebrano_koniec_wycieczki = 0;

    if (rola == KIBIC) {
    srand((unsigned)time(NULL) + (unsigned)pid);
}
    int organizator_startu = -1;
    unsigned long long zegar_lamporta = 0;
    while (1) {
        pytajnik_zaczepnik(rola, &zegar_lamporta);
        switch (rola) {
            case ORGANIZATOR:
                switch (stan_organizatora) {
                    case ORG_ZBIERA_ZALOGE: {
                        int jest_zgloszenie;
                        MPI_Status status;

                        MPI_Iprobe(MPI_ANY_SOURCE, TAG_ZGLOSZENIE,MPI_COMM_WORLD, &jest_zgloszenie, &status);
                        if (jest_zgloszenie) {
                            unsigned long long czas_zgloszenia;

                            MPI_Recv(&czas_zgloszenia, 1, MPI_UNSIGNED_LONG_LONG, status.MPI_SOURCE, TAG_ZGLOSZENIE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                            if (czas_zgloszenia > zegar_lamporta) {
                                zegar_lamporta = czas_zgloszenia;
                                }
                            zegar_lamporta++;

                            zaloga[liczba_zebranych] = status.MPI_SOURCE;
                            liczba_zebranych++;
                        }

                        if (liczba_zebranych == rozmiar_grupy) {
                            for (int i = 0; i < liczba_zebranych; i++) {
                                zegar_lamporta++;
                                MPI_Send(&zegar_lamporta, 1, MPI_UNSIGNED_LONG_LONG,
                                zaloga[i], TAG_START, MPI_COMM_WORLD);
                            }
                            czas_startu = MPI_Wtime();
                            stan_organizatora = ORG_PROWADZI_WYCIECZKE;
                            zegar_lamporta++;
                            printf("[%d] [t%llu] Organizator: rozpoczynam wycieczke z %d uczestnikami\n",
                            pid, zegar_lamporta, liczba_zebranych);
                            fflush(stdout);

                            for(int kibic = liczba_organizatorow;
                                kibic < pierwszy_uczestnik;
                                kibic++ ) {
                                    zegar_lamporta++;
                                    MPI_Send(&zegar_lamporta, 1, MPI_UNSIGNED_LONG_LONG,kibic, TAG_START_WYCIECZKI, MPI_COMM_WORLD);
                                }
                        }
                        break;
                    }



                    case ORG_PROWADZI_WYCIECZKE: {
                        if (kibic_spotkania != -1) {
                            stan_organizatora = ORG_BIJE_SIE;
                            break;
                        }

                        if (MPI_Wtime() - czas_startu >= CZAS_Wycieczki) {
                            stan_organizatora = ORG_KONIEC_WYCIECZKI;
                        }

                         break;
                    }

                    


                    case ORG_BIJE_SIE: {

                        int jest_wynik;
                        MPI_Iprobe(kibic_spotkania, TAG_WYNIK_SPOTKANIA, MPI_COMM_WORLD, &jest_wynik, MPI_STATUS_IGNORE);

                        if (!jest_wynik)
                        {
                            break;
                        }

                        unsigned long long dane_wyniku[2];

                        MPI_Recv(dane_wyniku, 2, MPI_UNSIGNED_LONG_LONG,kibic_spotkania, TAG_WYNIK_SPOTKANIA,MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                        int wygral_kibic = (int)dane_wyniku[0];
                        unsigned long long czas_wyniku = dane_wyniku[1];

                        if (czas_wyniku > zegar_lamporta) {
                            zegar_lamporta = czas_wyniku;
                        }
                        zegar_lamporta++;

                        kibic_spotkania = -1;

                        if (wygral_kibic == 0)
                        {
                            stan_organizatora = ORG_PROWADZI_WYCIECZKE;
                        }else {
                            for(int i = 0; i < liczba_zebranych; i++) {
                                zegar_lamporta++;
                                MPI_Send(&zegar_lamporta, 1, MPI_UNSIGNED_LONG_LONG, zaloga[i], TAG_KONIEC, MPI_COMM_WORLD);
                            }


                            liczba_zebranych = 0;
                            poczatek_szpitala = MPI_Wtime();
                            stan_organizatora = ORG_PRZEBYWA_W_SZPITALU;
                            zegar_lamporta++;
                            printf("[%d] [t%llu] Organizator: trafiam do szpitala\n",pid, zegar_lamporta);
                            fflush(stdout);
                        }
                        break;
                        
                    }
                        

                    case ORG_KONIEC_WYCIECZKI: {

                        zegar_lamporta++;
                        printf("[%d] [t%llu] Organizator: koncze wycieczke\n",pid, zegar_lamporta);
                        fflush(stdout);

                        for(int i = 0; i< liczba_zebranych; i++) {
                            zegar_lamporta++;
                            MPI_Send(&zegar_lamporta, 1, MPI_UNSIGNED_LONG_LONG,zaloga[i], TAG_KONIEC, MPI_COMM_WORLD);
                        }

                        liczba_zebranych = 0;
                        stan_organizatora = ORG_ZBIERA_ZALOGE;
                        break;
                    }



                    case ORG_PRZEBYWA_W_SZPITALU: {
                        if (MPI_Wtime() - poczatek_szpitala >= CZAS_SZPITALA) {
                            stan_organizatora = ORG_ZBIERA_ZALOGE;
                        }
                        break;
                        }


                    default:
                        break;

                }
                break;

            case KIBIC:
                switch (stan_kibica)
                {
                    case KIB_NORMALNY: {
                        int jest_start;
                        MPI_Status status_startu;

                        MPI_Iprobe(MPI_ANY_SOURCE, TAG_START_WYCIECZKI, MPI_COMM_WORLD,&jest_start, &status_startu);

                        if (jest_start) {
                            organizator_startu = status_startu.MPI_SOURCE;

                            unsigned long long czas_startu_lamporta;

                            MPI_Recv(&czas_startu_lamporta, 1, MPI_UNSIGNED_LONG_LONG,status_startu.MPI_SOURCE, TAG_START_WYCIECZKI,MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                            if (czas_startu_lamporta > zegar_lamporta) {
                                zegar_lamporta = czas_startu_lamporta;
                            }
                            zegar_lamporta++;

                            stan_kibica = KIB_SZUKA_ZACZEPKI;
                        }

                        break;
                    }


                case KIB_SZUKA_ZACZEPKI: {
                    if (zaczepka_kibica == MPI_REQUEST_NULL) {
                        if (adresat_zaczepki >= 0 && adresat_zaczepki < liczba_organizatorow) {
                            adresat_zaczepki = pierwszy_uczestnik;
                            } else {
                            adresat_zaczepki = organizator_startu;
                            }

                            zegar_lamporta++;
                            czas_wyslanej_zaczepki = zegar_lamporta;
                            MPI_Isend(&czas_wyslanej_zaczepki, 1, MPI_UNSIGNED_LONG_LONG,adresat_zaczepki, TAG_PYTANIE_O_ZACZEPKE,MPI_COMM_WORLD, &zaczepka_kibica);

                        }

                    int jest_start;
                    MPI_Status status_startu;
                    MPI_Iprobe(MPI_ANY_SOURCE, TAG_START_WYCIECZKI, MPI_COMM_WORLD,&jest_start, &status_startu);

                    if (jest_start) {
                     unsigned long long czas_startu_lamporta;

                    MPI_Recv(&czas_startu_lamporta, 1, MPI_UNSIGNED_LONG_LONG,status_startu.MPI_SOURCE, TAG_START_WYCIECZKI,MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                    if (czas_startu_lamporta > zegar_lamporta) {
                        zegar_lamporta = czas_startu_lamporta;
                    }
                    zegar_lamporta++;
                    }

                    int jest_odpowiedz;
                    MPI_Iprobe(adresat_zaczepki, TAG_ODPOWIEDZ_NA_ZACZEPKE, MPI_COMM_WORLD, &jest_odpowiedz, MPI_STATUS_IGNORE);

                    if (!jest_odpowiedz) {
                        break;
                        }

                
                    unsigned long long dane_odpowiedzi[2];

                    MPI_Recv(dane_odpowiedzi, 2, MPI_UNSIGNED_LONG_LONG,adresat_zaczepki, TAG_ODPOWIEDZ_NA_ZACZEPKE,MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                    int odpowiedz = (int)dane_odpowiedzi[0];
                    unsigned long long czas_odpowiedzi = dane_odpowiedzi[1];

                    if (czas_odpowiedzi > zegar_lamporta) {
                        zegar_lamporta = czas_odpowiedzi;
                    }
                    zegar_lamporta++;
                    MPI_Wait(&zaczepka_kibica, MPI_STATUS_IGNORE);

                    if (odpowiedz == 1) {
                        stan_kibica = KIB_BIJE_SIE;
                    } else {
                         stan_kibica = KIB_NORMALNY;
                        }

                    break;
                }

                case KIB_BIJE_SIE: {
                    int wygral = rand() % 2;
                    zegar_lamporta++;
                    unsigned long long dane_wyniku[2] = {
                        (unsigned long long)wygral,
                        zegar_lamporta
                    };

                    MPI_Send(dane_wyniku, 2, MPI_UNSIGNED_LONG_LONG,adresat_zaczepki, TAG_WYNIK_SPOTKANIA, MPI_COMM_WORLD);

                    if (wygral == 1) {
                        printf("[%d] [t%llu] Wygralem spotkanie\n", pid, zegar_lamporta);
                    } else {
                        printf("[%d] [t%llu] Przegralem spotkanie\n", pid, zegar_lamporta);
                    }

                    fflush(stdout);
                    poczatek_ukrywania = MPI_Wtime();
                    stan_kibica = KIB_UKRYWA_SIE;
                    break;
                }


                case KIB_UKRYWA_SIE: {
                    if (MPI_Wtime() - poczatek_ukrywania >= CZAS_UKRYWANIA) {
                        stan_kibica = KIB_NORMALNY;
                        break;
                    }

                    int jest_start;
                    MPI_Status status_startu;
                    MPI_Iprobe(MPI_ANY_SOURCE, TAG_START_WYCIECZKI, MPI_COMM_WORLD,&jest_start, &status_startu);


                    if (jest_start)
                    {
                        unsigned long long czas_startu_lamporta;

                        MPI_Recv(&czas_startu_lamporta, 1, MPI_UNSIGNED_LONG_LONG,status_startu.MPI_SOURCE, TAG_START_WYCIECZKI,MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                        if (czas_startu_lamporta > zegar_lamporta) {
                        zegar_lamporta = czas_startu_lamporta;
                        }
                        zegar_lamporta++;
                    }

                    break;
                    
                }
                
                }
                break;



            case UCZESTNIK:
                switch (stan_uczestnika) {
                    case UCZ_NORMALNY:
                        zegar_lamporta++;

                         printf("[%d] [t%llu] Ubiegam sie o miejsce na wycieczce\n",pid, zegar_lamporta);
                         fflush(stdout);

                        MPI_Send(&zegar_lamporta, 1, MPI_UNSIGNED_LONG_LONG, moj_organizator, TAG_ZGLOSZENIE, MPI_COMM_WORLD);

                        stan_uczestnika = UCZ_UBIEGA_SIE_O_WYCIECZKE;
                        break;

                    case UCZ_UBIEGA_SIE_O_WYCIECZKE: {
                        int jest_start;

                        MPI_Iprobe(moj_organizator, TAG_START, MPI_COMM_WORLD,
                                   &jest_start, MPI_STATUS_IGNORE);

                        if (jest_start) {
                            unsigned long long czas_startu_lamporta;

                            MPI_Recv(&czas_startu_lamporta, 1, MPI_UNSIGNED_LONG_LONG,moj_organizator, TAG_START, MPI_COMM_WORLD,MPI_STATUS_IGNORE);

                            if (czas_startu_lamporta > zegar_lamporta) {
                                zegar_lamporta = czas_startu_lamporta;
                            }
                        zegar_lamporta++;

                        stan_uczestnika = UCZ_UCZESTNICZY_W_WYCIECZCE;

                        printf("[%d] [t%llu] Rozpoczynam wycieczke\n", pid, zegar_lamporta);
                        fflush(stdout);
                        }

                        break;
                    }

                    case UCZ_UCZESTNICZY_W_WYCIECZCE: {

                        if (kibic_spotkania != -1) {
                            stan_uczestnika = UCZ_BIJE_SIE;
                            break;
                        }

                        int jest_koniec;

                        MPI_Iprobe(moj_organizator, TAG_KONIEC, MPI_COMM_WORLD, &jest_koniec, MPI_STATUS_IGNORE);

                        if(jest_koniec) {
                            unsigned long long czas_konca_lamporta;

                            MPI_Recv(&czas_konca_lamporta, 1, MPI_UNSIGNED_LONG_LONG,moj_organizator, TAG_KONIEC, MPI_COMM_WORLD,MPI_STATUS_IGNORE);

                            if (czas_konca_lamporta > zegar_lamporta) {
                                zegar_lamporta = czas_konca_lamporta;
                            }
                            zegar_lamporta++;

                            printf("[%d] [t%llu] Zwalniam miejsce na wycieczce\n",pid, zegar_lamporta);
                            fflush(stdout);

                            stan_uczestnika = UCZ_NORMALNY;
                        }


                        break;

                    }



                    case UCZ_BIJE_SIE: {
                        int jest_wynik;

                        MPI_Iprobe(kibic_spotkania, TAG_WYNIK_SPOTKANIA, MPI_COMM_WORLD, &jest_wynik, MPI_STATUS_IGNORE);

                        if (!jest_wynik)
                        {
                            break;
                        }
                        unsigned long long dane_wyniku[2];

                        MPI_Recv(dane_wyniku, 2, MPI_UNSIGNED_LONG_LONG,kibic_spotkania, TAG_WYNIK_SPOTKANIA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                        int wygral_kibic = (int)dane_wyniku[0];
                        unsigned long long czas_wyniku = dane_wyniku[1];

                        if (czas_wyniku > zegar_lamporta) {
                            zegar_lamporta = czas_wyniku;
                        }
                        zegar_lamporta++;
                        kibic_spotkania = -1;

                           if (wygral_kibic == 0)
                           {
                            stan_uczestnika = UCZ_UCZESTNICZY_W_WYCIECZCE;
                           }else {
                            odebrano_koniec_wycieczki = 0;
                            poczatek_szpitala = MPI_Wtime();
                            stan_uczestnika = UCZ_SZPITAL;
                            zegar_lamporta++;
                            printf("[%d] [t%llu] Uczestnik: trafiam do szpitala\n",pid, zegar_lamporta);
                            fflush(stdout);
                           }
                        break;
                           
                    }

                    case UCZ_SZPITAL: {
                        if (!odebrano_koniec_wycieczki)
                        {
                            int jest_koniec;
                            MPI_Iprobe(moj_organizator, TAG_KONIEC, MPI_COMM_WORLD, &jest_koniec, MPI_STATUS_IGNORE);

                            if (jest_koniec)
                            {
                            unsigned long long czas_konca_lamporta;

                            MPI_Recv(&czas_konca_lamporta, 1, MPI_UNSIGNED_LONG_LONG,moj_organizator, TAG_KONIEC, MPI_COMM_WORLD,MPI_STATUS_IGNORE);

                            if (czas_konca_lamporta > zegar_lamporta) {
                                zegar_lamporta = czas_konca_lamporta;
                            }
                            zegar_lamporta++;

                            printf("[%d] [t%llu] Zwalniam miejsce na wycieczce\n",pid, zegar_lamporta);
                            fflush(stdout);
                            odebrano_koniec_wycieczki = 1;
                            }
                        }

                        if (odebrano_koniec_wycieczki && MPI_Wtime() - poczatek_szpitala >= CZAS_SZPITALA) {
                            stan_uczestnika = UCZ_NORMALNY;
                        }

                        break;

                    }
                    default:
                        break;
                }
                break;
        }
    }
}