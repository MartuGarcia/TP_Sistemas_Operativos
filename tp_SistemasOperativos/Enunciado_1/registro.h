#ifndef REGISTRO_H
#define REGISTRO_H

#include <stdio.h>

#define LLAVE_BUF 0x1234
#define LLAVE_CTRL 0x5678

#define ELEMENTOS_BUF 128     // tamaño del buffer circular en SHM (ajustable)
#define REGS_A_LEER 10       // bloque solicitado por productor (debe ser 10 según consigna)
#define MAX_PRODS 64         // número máximo de productores soportados
#define MAX_RECYCLE 256      // cantidad máxima de bloques reciclados

typedef struct {
    int id;
    int productor_idx;   // 0-based
    char nombre[64];
    int stock;
    double precio;
} Registro;

typedef struct {
    int start;
    int cantidad;
    int asignado; // 1 si está asignado a algún productor
} BloqueIDs;

typedef struct {
    int start;
    int cantidad;
} RecycleRange;

/* Control compartido en SHM */
typedef struct {
    int next_id;    // siguiente id libre (monótono creciente)
    int remaining;  // cantidad total restante por asignar (incluye reciclados)
    BloqueIDs bloques[MAX_PRODS];  // bloque asignado por productor idx
    int requests[MAX_PRODS];       // 1 si productor idx solicitó bloque
    RecycleRange recycle[MAX_RECYCLE];
    int recycle_count;
} Control;

/* Prototipos */
int abrir_archivo_append(FILE **f, const char *ruta);
int escribir_registro_csv(FILE *f, const Registro *r);
Registro generar_registro_aleatorio(int id, int productor_idx);

#endif // REGISTRO_H


