// registro.c
#include "registro.h"

int abrir_archivo_append(FILE **f, const char *ruta){
    (void)ruta;
    *f = fopen("registros.csv", "a");
    if(!*f) return -1;
    return 0;
}

int escribir_registro_csv(FILE *f, const Registro *r){
    if(!f || !r) return -1;
    if(fprintf(f, "%d,%d,%s,%d,%.2f\n", r->id, r->productor_idx, r->nombre, r->stock, r->precio) < 0)
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
    r.stock = (rand() % 100) + 1; // 1..100
    r.precio = ((rand() % 2000) + 100) / 100.0; // 1.00 .. 20.99
    return r;
}

