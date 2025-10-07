// registro.c
#include "registro.h"
#include <string.h>
#include <stdlib.h>

int abrir_archivo_append(FILE **f, const char *ruta){
    const char *use = ruta ? ruta : "registros.csv";
    *f = fopen(use, "a+"); /* a+ para poder verificar tamaño y append */
    if(!*f) return -1;
    return 0;
}

int escribir_registro_csv(FILE *f, const Registro *r){
    if(!f || !r) return -1;
    /* escribo productor como 1-based para legibilidad (si preferís 0-based, cambiar) */
    if(fprintf(f, "%d,%d,%s,%d,%.2f\n", r->id, r->productor_idx+1, r->nombre, r->stock, r->precio) < 0)
        return -1;
    return 0;
}

Registro generar_registro_aleatorio(int id, int productor_idx){
    Registro r;
    r.id = id;
    r.productor_idx = productor_idx;
    const char *nombres[] = {"Paracetamol","Ibuprofeno","Amoxicilina","Atorvastatina","Omeprazol","Loratadina","Cetirizina"};
    int n = sizeof(nombres)/sizeof(nombres[0]);
    const char *sel = nombres[rand() % n];
    strncpy(r.nombre, sel, sizeof(r.nombre)-1);
    r.nombre[sizeof(r.nombre)-1] = '\0';
    r.stock = (rand() % 100) + 1; /* 1..100 */
    r.precio = ((rand() % 2000) + 100) / 100.0; /* 1.00 .. 20.99 */
    return r;
}
