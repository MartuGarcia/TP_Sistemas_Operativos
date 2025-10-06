#ifndef REGISTRO_H
#define REGISTRO_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define REGS_A_LEER 10
#define ELEMENTOS_BUF 5
#define LLAVE_BUF 1759
#define LLAVE_CTRL 2005

typedef struct {
    int id;             // ID del registro (0 = libre)
    int productor_idx;  // índice del productor que creó el registro
    char nombre[64];    // ejemplo: nombre de medicamento
    int stock;          // cantidad en stock
    double precio;      // precio
} Registro;

/* funciones auxiliares */
int abrir_archivo_append(FILE **f, const char *ruta);
int escribir_registro_csv(FILE *f, const Registro *r);
Registro generar_registro_aleatorio(int id, int productor_idx);

#endif

