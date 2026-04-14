#include <msp430.h>

typedef enum {NORMAL, PENALTI, FALTA} tEstado;
typedef enum {A_NORMAL, A_GOL, A_PENALTI, A_FALTA, A_J1J2, A_J2J1} tAnimacion;
//bits 0 a 5 de la bateria. Registro LCDM14 y LCDM18. PATRONES CON BATERIA. 
// A_NORMAL     111111
// A_GOL        000000 -> 111111
// A_PENALTI    010101 -> 101010
// A_FALTA      110011 -> 001100
// A_J1J2       100000 -> 010000 -> 001000 -> ...
// A_J2J1       000001 -> 000010 -> 000100 -> ...

const char MENSAJEINICIO[] = "COMIENZA EL PARTIDO: PULSAD LA TECLA P/ I PARA ALTERNAR ENTRE CENTESIMAS PARES/ IMPARES AL TIRAR UN PENALTI O CUALQUIER OTRA TECLA PARA VER EL MARCADOR\n";
const char MENSAJEGOL[] = "GOL DEL JUGADOR ";
const char MENSAJEPENALTI[] = "PENALTI PARA JUGADOR ";
const char MENSAJEFALTA[] = "FALTA PARA JUGADOR ";
const char MENSAJEVICTORIA[] = "\nFIN DEL PARTIDO: VICTORIA PARA JUGADOR ";
const char MENSAJEEMPATE[] = "\nFIN DEL PARTIDO: EMPATE";
//
const unsigned char LCDNUM[10] = {0xFC, 0x60, 0xDB, 0xF3, 0x67, 0xB7, 0xBF, 0xE0, 0xFF, 0xE7};
const unsigned int MAXTIME = 9000; // En centisegundos
const int MAXGOLES = 5;
const int ANIMACION = 6; //num de interrupciones

char mensajeAMandar[200];
volatile unsigned int contMensaje = 0;
volatile unsigned int centiseg = 0;
volatile int goles1 = 0, goles2 = 0;
volatile tEstado estadoActual = NORMAL;
volatile tAnimacion animacionActual = A_NORMAL;
volatile int par = 0;
int tiempoAnimacion = 6;

void initPins(void);
void initClock(void);
void initTimer(void);
void initLCD(void);
void initUART(void);
void display(void); 
void accionJugador(int jugador, int rival);
int partidoTerminado(void);
void terminarPartido(void);
void jugada(int jugador, int rival);
void gol(int jugador);
void penalti(int jugador);
void falta(int jugador);
void pasarTurno(int jugador, int rival);
void recibirCaracter(void);
void mostrarEleccion(void);
void mostrarMarcador(void);
void enviarMensaje(const char* mensaje, int jugador);
char nombreJugador(int jugador);
void enviarCaracter(void);

//funciones de animaciones.
void interrupcionAnimacion(void);
void avanzarAnimacion(void);
void setAnimacion(tAnimacion animacion);
void animacionOpuesta(void);
void desplazarDer(void);
void desplazarIzq(void);
void movimientoIzqDer(int momento);

int main(void) {
    WDTCTL = WDTPW | WDTHOLD; 
    PM5CTL0 &= ~LOCKLPM5;

    initPins();
    initClock();
    initTimer();
    initLCD();
    initUART();
    display();
    enviarMensaje(MENSAJEINICIO, 0);
    setAnimacion(A_NORMAL);
    _BIS_SR(LPM0_bits + GIE);
}

//RTIs
#pragma vector = PORT1_VECTOR
__interrupt void BUTTONS(void) {
    switch(__even_in_range(P1IV, 0x10)) {
        case 0: break; // Sin interrupciones
        case 4: accionJugador(BIT1, BIT2); break; // P1.1
        case 6: accionJugador(BIT2, BIT1); break; // P1.2
        default:
    }
}

#pragma vector = TIMER0_A0_VECTOR
__interrupt void ANIMATION_FAST(void) {
    interrupcionAnimacion();
}

#pragma vector = TIMER0_A1_VECTOR
__interrupt void ANIMATION_SLOW(void) {
    if(TA0IV == 14) interrupcionAnimacion(); //overflow
}

void interrupcionAnimacion(void){
    tiempoAnimacion--;
    if(tiempoAnimacion > 0) avanzarAnimacion();
    else {
        tiempoAnimacion = ANIMACION;
        setAnimacion(A_NORMAL);
    }
}

#pragma vector = TIMER1_A0_VECTOR
__interrupt void TIMER(void) {
    centiseg++;
    display();
}

#pragma vector = USCI_A1_VECTOR 
__interrupt void USCI_A1_ISR(void) {
    switch(__even_in_range(UCA1IV, 0x08)) {
        case 0: break; // Sin interrupciones
        case 2: recibirCaracter(); break; // Letra recibida
        case 4: enviarCaracter(); break; // Buffer de transmisión vacío
        default:
    }
}

// Funciones para el partido
void accionJugador(int jugador, int rival) {
    TA1CCTL0 ^= CCIE;
    P1IFG &= ~(jugador | rival);
    if(!(P1IES & jugador)) {
        if(partidoTerminado()) terminarPartido();
        else {
            switch(estadoActual) {
                case NORMAL:
                    jugada(jugador, rival);
                    break;
                case PENALTI:
                    if(par == 0) {
                         if(centiseg % 2 == 0) gol(jugador);
                    }
                    else if(centiseg % 2 == 1) gol(jugador);
                    estadoActual = NORMAL;
                    break;
                case FALTA: 
                    if(centiseg % 10 == 0) gol(jugador); 
                    estadoActual = NORMAL;
                    break;
            }
            if(estadoActual == NORMAL) pasarTurno(jugador, rival);
        }
    }
    P1IES ^= jugador; 
}

int partidoTerminado(void) {
    return centiseg >= MAXTIME || goles1 >= MAXGOLES || goles2 >= MAXGOLES;
}

void terminarPartido(void) {
    P1IE &= ~(BIT1 | BIT2);
    P1OUT |= BIT0;
    P9OUT |= BIT7;
    if(goles1 == goles2) enviarMensaje(MENSAJEEMPATE, 0);
    else enviarMensaje(MENSAJEVICTORIA, goles1 > goles2 ? BIT1 : BIT2);
}

void jugada(int jugador, int rival) {
    switch(centiseg % 100) {
        case 00: gol(jugador); break;
        case 01: penalti(jugador); break;
        case 02: falta(jugador); break;
        case 99: 
            penalti(rival);
            pasarTurno(jugador, rival);
            break;
        case 98:
            falta(rival);
            pasarTurno(jugador, rival);
            break;
        default:
    }
}

void gol(int jugador) {
    if(jugador == BIT1) goles1++;
    else goles2++;
    enviarMensaje(MENSAJEGOL, jugador);
    setAnimacion(A_GOL);
}

void penalti(int jugador) {
    estadoActual = PENALTI;
    enviarMensaje(MENSAJEPENALTI, jugador);
    setAnimacion(A_PENALTI);
}

void falta(int jugador){
    estadoActual = FALTA;
    enviarMensaje(MENSAJEFALTA, jugador);
    setAnimacion(A_FALTA);
}

void pasarTurno(int jugador, int rival){
    P1IE &= ~jugador;
    P1IE |= rival;
    setAnimacion(jugador == BIT1 ? A_J1J2 : A_J2J1);
    // Apagamos el LED del jugador y encendemos el del rival
    if(jugador == BIT1){ 
        P1OUT &= ~BIT0;
        P9OUT |= BIT7;
    }
    else { 
        P1OUT |= BIT0;
        P9OUT &= ~BIT7;
    }
}

// LCD y animaciones
void display(void){ 
    LCDM7 |= 0x04; // Activa los dos puntos del bloque A2
    LCDM20 |= 0x04; // Activa los dos puntos del bloque A4

    int centesimas = centiseg % 100;
    LCDM8 = LCDNUM[centesimas % 10];
    LCDM15 = LCDNUM[centesimas / 10];

    int segundos = centiseg / 100 % 60;
    LCDM19 = LCDNUM[segundos % 10]; 
    LCDM4 = LCDNUM[segundos / 10]; 

    int minutos = centiseg / 100 / 60 % 60;
    LCDM6 = LCDNUM[minutos % 10]; // Solo llega hasta 1
}

void avanzarAnimacion(void){
    switch(animacionActual){ 
        case A_NORMAL: break; //no definimos cambios.
        case A_GOL: 
        case A_PENALTI:  
        case A_FALTA: animacionOpuesta();  break;
        case A_J1J2: desplazarDer(); break;
        case A_J2J1: desplazarIzq(); break;
    }
}

void animacionOpuesta(void){
    LCDM14 ^= (BIT5 | BIT6 | BIT7);
    LCDM18 ^= (BIT5 | BIT6 | BIT7);
}

void desplazarDer(void){
    movimientoIzqDer(tiempoAnimacion);
}

void desplazarIzq(void){
    movimientoIzqDer(ANIMACION - tiempoAnimacion + 1);
}

void movimientoIzqDer(int momento){
    LCDM14 = LCDM18 = 0;
    switch(momento){
        case 6: LCDM18 = BIT5; break;
        case 5: LCDM14 = BIT5; break;
        case 4: LCDM18 = BIT6; break;
        case 3: LCDM14 = BIT6; break;
        case 2: LCDM18 = BIT7; break;
        case 1: LCDM14 = BIT7; break;
    }
}

void setAnimacion(tAnimacion animacion){
    if(animacionActual == A_NORMAL || animacion == A_NORMAL) {
        LCDM14 = LCDM18 = 0;
        tiempoAnimacion = ANIMACION;
        animacionActual = animacion;
        switch(animacion){ 
        case A_NORMAL:
        case A_GOL: LCDM18 = LCDM14 = (BIT5 | BIT6 | BIT7); break;
        case A_PENALTI:  LCDM14 = (BIT5 | BIT6 | BIT7); break;
        case A_FALTA: LCDM18 = LCDM14 = (BIT5 | BIT7);  break;
        case A_J1J2: LCDM18 = BIT5; break;
        case A_J2J1: LCDM14 = BIT7; break;
        }
    }
}


// UART
void recibirCaracter(void) {
    char car = UCA1RXBUF;

    if(estadoActual == PENALTI && (car == 'P' || car == 'p' || car == 'I' || car == 'i')) {
        if(car == 'P' || car == 'p') par = 0;
        else par = 1;
        mostrarEleccion();
    }
    else mostrarMarcador();
}

void mostrarEleccion(void) {
    const char plantilla[] = "CENTESIMAS ELEGIDAS: ";
    char* eleccion;
    int i = 0, j = 0;

    if(par == 0) eleccion = "PARES\n"; 
    else eleccion = "IMPARES\n"; 
    while(plantilla[i] != '\0') {
        mensajeAMandar[i] = plantilla[i];
        i++;
    }
    while(eleccion[j] != '\0') {
        mensajeAMandar[i++] = eleccion[j++];
    }
    mensajeAMandar[i] = '\0'; 
    contMensaje = 0;
    UCA1TXBUF = mensajeAMandar[contMensaje++];
    UCA1IE |= UCTXIE;
}

void mostrarMarcador(void) {
    const char plantilla[] = "\n[MARCADOR] J1: 0   J2: 0 [MARCADOR]\n";
    int i = 0;

    while(plantilla[i] != '\0') {
        mensajeAMandar[i] = plantilla[i];
        i++;
    }
    mensajeAMandar[i] = '\0';
    mensajeAMandar[16] = '0' + goles1; 
    mensajeAMandar[24] = '0' + goles2; 
    contMensaje = 0;
    UCA1TXBUF = mensajeAMandar[contMensaje++];
    UCA1IE |= UCTXIE;
}

void enviarMensaje(const char *mensaje, int jugador) {
    char nombre = nombreJugador(jugador);
    int i = 0;

    while(mensaje[i] != '\0') {
        mensajeAMandar[i] = mensaje[i];
        i++;
    }
    mensajeAMandar[i++] = nombre;
    mensajeAMandar[i++] = '\n';
    mensajeAMandar[i]   = '\0';
    contMensaje = 0;
    UCA1TXBUF = mensajeAMandar[contMensaje++];
    UCA1IE |= UCTXIE;
}

char nombreJugador(int jugador) {
    if(jugador == BIT1) return '1';
    else if(jugador == BIT2) return '2';
    else return ' ';
}

void enviarCaracter(void) {
    if(mensajeAMandar[contMensaje] != '\0') UCA1TXBUF = mensajeAMandar[contMensaje++];
    else UCA1IE &= ~UCTXIE;
}

// Inicialización
void initPins(void) {
    // UART
    P3SEL0 |= BIT4 | BIT5;
    P3SEL1 &= ~(BIT4 | BIT5);

    // Botones (empieza jugador rojo)
    P1DIR &= ~(BIT1 | BIT2);
    P1REN |= BIT1 | BIT2;
    P1OUT |= BIT1 | BIT2;
    P1IE |= BIT1;
    P1IE &= ~BIT2;
    P1IES |= BIT1 | BIT2;
    P1IFG &= ~(BIT1 | BIT2);

    // LEDs
    P1DIR |= BIT0;
    P9DIR |= BIT7;
    P1OUT |= BIT0;
    P9OUT &= ~BIT7;
}

void initClock(void) {
    CSCTL0_H = CSKEY >> 8; // Unlock clock registers
    CSCTL1 = DCOFSEL_3 | DCORSEL; // Set DCO to 8MHz
    CSCTL2 = SELA__LFXTCLK | SELS__DCOCLK | SELM__DCOCLK;
    CSCTL3 = DIVA__1 | DIVS__1 | DIVM__1; // Set all dividers
    CSCTL0_H = 0; // Lock CS registers
}

void initTimer(void) { 
    TA1CTL = TASSEL__ACLK | TACLR | MC_1;
    TA1CCR0 = 327;
    TA1CCTL0 &= ~CCIE;

    TA0CTL = TASSEL__SMCLK | TACLR | MC_2 | TAIE | ID__8;
    TA0EX0 = TAIDEX_4; //DIVISOR ADICIONAL
    TA0CCR0 = 16384; //la cuarta parte del overflow
    TA0CCTL0 |= CCIE;
}

void initLCD(void) { 
    // Configurar los segmentos del LCD (LaunchPad FR6989)
    LCDCPCTL0 = 0xFFFF;
    LCDCPCTL1 = 0xFC3F;
    LCDCPCTL2 = 0x0FFF;

    // Configurar LCD: ACLK, divisor 1, predivisor 16, modo 4-MUX
    LCDCCTL0 = LCDDIV__1 | LCDPRE__16 | LCD4MUX | LCDLP;

    // Configurar voltaje del LCD (VLCD ≈ 3.02 V)
    LCDCVCTL = VLCD_3 | VLCDREF_0 | LCDCPEN;

    // Sincronización del charge pump
    LCDCCPCTL = LCDCPCLKSYNC;

    // Limpiar memoria del LCD
    LCDCMEMCTL = LCDCLRM;

    // Encender LCD
    LCDCCTL0 |= LCDON;
}

void initUART(void) {
    UCA1CTLW0 = UCSWRST; // Put eUSCI in reset
    UCA1CTLW0 |= UCSSEL__SMCLK; // CLK = SMCLK              
    UCA1BR0 = 52;                             
    UCA1BR1 = 0x00;                                                               
    UCA1MCTLW |= UCOS16 | UCBRF_1 | 0x4900;
    UCA1CTLW0 &= ~UCSWRST; // Initialize eUSCI
    // Configure interruptions to receive and transmit data in register UCA1IE
    UCA1IE &= ~UCTXIE;
    UCA1IE |= UCRXIE;
}
