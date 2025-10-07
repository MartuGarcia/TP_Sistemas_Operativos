// main.c
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include "registro.h"

const char *SEM_ID_NAME  = "/sem_ID_tp";
const char *SEM_BR_NAME  = "/sem_BR_tp";
const char *SEM_MB_NAME  = "/sem_MB_tp";
const char *SEM_EB_NAME  = "/sem_EB_tp";
const char *SEM_AU_NAME  = "/sem_AU_tp";
const char *SEM_SOLICITUDES = "/sem_solicitudes_tp"; // contador de solicitudes

/* globales para cleanup */
int shmbuf = -1, shmctrl = -1;
sem_t *semIDs=NULL, *semBuf=NULL, *semMsgBuf=NULL, *semEspBuf=NULL, *semArchAux=NULL, *semSolicitudes=NULL;
sem_t **semRespuesta = NULL;  // array de semáforos por productor para recibir asignación
pid_t *child_pids = NULL;
int cantProcs_glob = 0;
int totalIds_glob = 0;
volatile sig_atomic_t child_died = 0;
volatile sig_atomic_t terminate_signal = 0;

/* helpers */
static int sem_wait_retry(sem_t *s){
    int rc;
    while ((rc = sem_wait(s)) == -1 && errno == EINTR) {}
    return rc;
}
static int sem_post_retry(sem_t *s){
    int rc;
    while ((rc = sem_post(s)) == -1 && errno == EINTR) {}
    return rc;
}

void cleanup_ipc_and_exit(int status){
    if(child_pids){
        for(int i=0;i<cantProcs_glob;i++){
            if(child_pids[i] > 0){
                kill(child_pids[i], SIGTERM);
            }
        }
    }
    if(semIDs) sem_close(semIDs);
    if(semBuf) sem_close(semBuf);
    if(semMsgBuf) sem_close(semMsgBuf);
    if(semEspBuf) sem_close(semEspBuf);
    if(semArchAux) sem_close(semArchAux);
    if(semSolicitudes) sem_close(semSolicitudes);
    if(semRespuesta){
        for(int i=0;i<cantProcs_glob;i++){
            if(semRespuesta[i]) sem_close(semRespuesta[i]);
            char name[64];
            snprintf(name, sizeof(name), "/sem_res_tp_%d", i);
            sem_unlink(name);
        }
        free(semRespuesta);
    }

    sem_unlink(SEM_ID_NAME);
    sem_unlink(SEM_BR_NAME);
    sem_unlink(SEM_MB_NAME);
    sem_unlink(SEM_EB_NAME);
    sem_unlink(SEM_AU_NAME);
    sem_unlink(SEM_SOLICITUDES);

    if(shmbuf >= 0) {
        shmctl(shmbuf, IPC_RMID, NULL);
    }
    if(shmctrl >= 0) {
        shmctl(shmctrl, IPC_RMID, NULL);
    }

    free(child_pids);
    _exit(status);
}

void sigint_handler(int sig){
    (void)sig;
    terminate_signal = 1;
}

void sigchld_handler(int sig){
    (void)sig;
    child_died = 1;
}

/* productor ahora sólo solicita y espera asignación del coordinador */
int productor_func(int idx){
    /* abrir sems nombrados */
    sem_t *sIDs = sem_open(SEM_ID_NAME, 0);
    sem_t *sBuf = sem_open(SEM_BR_NAME, 0);
    sem_t *sMsg = sem_open(SEM_MB_NAME, 0);
    sem_t *sEsp = sem_open(SEM_EB_NAME, 0);
    sem_t *sArch = sem_open(SEM_AU_NAME, 0);
    sem_t *sSolic = sem_open(SEM_SOLICITUDES, 0);
    char name_resp[64];
    snprintf(name_resp, sizeof(name_resp), "/sem_res_tp_%d", idx);
    sem_t *sResp = sem_open(name_resp, 0);

    if(sIDs==SEM_FAILED || sBuf==SEM_FAILED || sMsg==SEM_FAILED || sEsp==SEM_FAILED || sArch==SEM_FAILED || sSolic==SEM_FAILED || sResp==SEM_FAILED){
        perror("productor sem_open");
        return 1;
    }

    int local_shmbuf = shmget(LLAVE_BUF, ELEMENTOS_BUF * sizeof(Registro), 0666);
    int local_shmctrl = shmget(LLAVE_CTRL, sizeof(Control), 0666);
    if(local_shmbuf < 0 || local_shmctrl < 0){
        perror("productor shmget");
        return 1;
    }

    Registro *buf = (Registro*) shmat(local_shmbuf, NULL, 0);
    Control  *ctrl = (Control*) shmat(local_shmctrl, NULL, 0);
    if(buf == (void*)-1 || ctrl == (void*)-1){
        perror("productor shmat");
        return 1;
    }

    srand((unsigned)(time(NULL) ^ getpid()));

    while(1){
        /* primero, verificar si ya no quedan IDs */
        if( sem_wait_retry(sIDs) == -1 ){ perror("sem_wait sIDs"); break; }
        int rem = ctrl->remaining;
        if( rem <= 0 ){
            sem_post_retry(sIDs);
            break;
        }
        /* indicar solicitud */
        ctrl->requests[idx] = 1;
        sem_post_retry(sIDs);

        /* avisar al coordinador (incrementa contador solicitudes) */
        sem_post_retry(sSolic);

        /* esperar respuesta del coordinador */
        if( sem_wait_retry(sResp) == -1 ){ perror("sem_wait sResp"); break; }

        /* leer bloque asignado */
        if( sem_wait_retry(sIDs) == -1 ){ perror("sem_wait sIDs"); break; }
        BloqueIDs bl = ctrl->bloques[idx];
        /* marcamos que ahora productor está ejecutando ese bloque (asignado==1). */
        /* No liberamos aquí; cuando termine el productor dejará asignado=0 */
        sem_post_retry(sIDs);

        if(bl.asignado == 0 || bl.cantidad <= 0){
            /* nada asignado (posible fin) */
            continue;
        }

        for(int k=0;k<bl.cantidad;k++){
            int id_actual = bl.start + k;
            Registro r = generar_registro_aleatorio(id_actual, idx);

            /* poner en buffer (produce uno por uno) */
            if( sem_wait_retry(sEsp) == -1 ){ perror("sem_wait sEsp"); break; }
            if( sem_wait_retry(sBuf) == -1 ){ perror("sem_wait sBuf"); sem_post_retry(sEsp); break; }

            int placed = 0;
            for(int i=0;i<ELEMENTOS_BUF;i++){
                if(buf[i].id == 0){
                    buf[i] = r;
                    placed = 1;
                    break;
                }
            }
            sem_post_retry(sBuf);
            if(!placed){
                /* devolver espacio y reintentar */
                sem_post_retry(sEsp);
                usleep(1000);
                k--; /* reintentar mismo id */
                continue;
            }
            sem_post_retry(sMsg);
            printf("[P%d] puso ID %d en buffer\n", idx+1, id_actual);
        }

        /* marcar bloque como consumido (liberar asignación) */
        if( sem_wait_retry(sIDs) == -1 ){ perror("sem_wait sIDs"); break; }
        ctrl->bloques[idx].asignado = 0;
        ctrl->bloques[idx].cantidad = 0;
        ctrl->bloques[idx].start = 0;
        sem_post_retry(sIDs);
    }

    shmdt(buf);
    shmdt(ctrl);
    sem_close(sIDs);
    sem_close(sBuf);
    sem_close(sMsg);
    sem_close(sEsp);
    sem_close(sArch);
    sem_close(sSolic);
    sem_close(sResp);
    return 0;
}

/* aux: obtener primer rango reciclado (si existe). devuelve cantidad devuelta o 0 */
static int pop_recycle(Control *ctrl, int *out_start, int *out_cant){
    if(ctrl->recycle_count <= 0) return 0;
    /* tomar el último (LIFO) */
    int idx = ctrl->recycle_count - 1;
    *out_start = ctrl->recycle[idx].start;
    *out_cant  = ctrl->recycle[idx].cantidad;
    ctrl->recycle_count--;
    return *out_cant;
}

int main(int argc, char *argv[]){
    if(argc != 3){
        fprintf(stderr, "Uso: %s <cant_productores> <cant_registros_totales>\n", argv[0]);
        return 1;
    }
    /* validar con strtol */
    char *end;
    errno = 0;
    long cp = strtol(argv[1], &end, 10);
    if(errno != 0 || *end != '\0' || cp <= 0 || cp > MAX_PRODS){
        fprintf(stderr,"Parametro productores invalido (1..%d)\n", MAX_PRODS);
        return 1;
    }
    errno = 0;
    long ti = strtol(argv[2], &end, 10);
    if(errno != 0 || *end != '\0' || ti <= 0){
        fprintf(stderr,"Parametro total registros invalido\n");
        return 1;
    }

    int cantProcs = (int)cp;
    int totalIds  = (int)ti;
    cantProcs_glob = cantProcs;
    totalIds_glob = totalIds;

    /* limpiar sems viejos */
    sem_unlink(SEM_ID_NAME);
    sem_unlink(SEM_BR_NAME);
    sem_unlink(SEM_MB_NAME);
    sem_unlink(SEM_EB_NAME);
    sem_unlink(SEM_AU_NAME);
    sem_unlink(SEM_SOLICITUDES);
    for(int i=0;i<cantProcs;i++){
        char name[64];
        snprintf(name, sizeof(name), "/sem_res_tp_%d", i);
        sem_unlink(name);
    }

    semIDs     = sem_open(SEM_ID_NAME, O_CREAT|O_EXCL, 0644, 1);
    semBuf     = sem_open(SEM_BR_NAME, O_CREAT|O_EXCL, 0644, 1);
    semMsgBuf  = sem_open(SEM_MB_NAME, O_CREAT|O_EXCL, 0644, 0);
    semEspBuf  = sem_open(SEM_EB_NAME, O_CREAT|O_EXCL, 0644, ELEMENTOS_BUF);
    semArchAux = sem_open(SEM_AU_NAME, O_CREAT|O_EXCL, 0644, 1);
    semSolicitudes = sem_open(SEM_SOLICITUDES, O_CREAT|O_EXCL, 0644, 0);
    if(semIDs==SEM_FAILED || semBuf==SEM_FAILED || semMsgBuf==SEM_FAILED || semEspBuf==SEM_FAILED || semArchAux==SEM_FAILED || semSolicitudes==SEM_FAILED){
        perror("sem_open");
        cleanup_ipc_and_exit(1);
    }

    /* crear semáforos respuesta por productor */
    semRespuesta = calloc(cantProcs, sizeof(sem_t*));
    if(!semRespuesta){ perror("calloc semRespuesta"); cleanup_ipc_and_exit(1); }
    for(int i=0;i<cantProcs;i++){
        char name[64];
        snprintf(name, sizeof(name), "/sem_res_tp_%d", i);
        semRespuesta[i] = sem_open(name, O_CREAT|O_EXCL, 0644, 0);
        if(semRespuesta[i] == SEM_FAILED){
            perror("sem_open respuesta");
            cleanup_ipc_and_exit(1);
        }
    }

    shmbuf = shmget(LLAVE_BUF, ELEMENTOS_BUF * sizeof(Registro), IPC_CREAT | 0666);
    shmctrl = shmget(LLAVE_CTRL, sizeof(Control), IPC_CREAT | 0666);
    if(shmbuf < 0 || shmctrl < 0){
        perror("shmget");
        cleanup_ipc_and_exit(1);
    }

    Registro *buf = (Registro*) shmat(shmbuf, NULL, 0);
    Control *ctrl = (Control*) shmat(shmctrl, NULL, 0);
    if(buf == (void*)-1 || ctrl == (void*)-1){
        perror("shmat");
        cleanup_ipc_and_exit(1);
    }

    /* inicializar buffer y control */
    for(int i=0;i<ELEMENTOS_BUF;i++) buf[i].id = 0;
    memset(ctrl,0,sizeof(Control));
    ctrl->next_id = 1;
    ctrl->remaining = totalIds;
    ctrl->recycle_count = 0;
    for(int i=0;i<cantProcs;i++){
        ctrl->requests[i] = 0;
        ctrl->bloques[i].asignado = 0;
    }

    /* preparar archivo CSV y escribir cabecera */
    FILE *csv = NULL;
    if( abrir_archivo_append(&csv, "registros.csv") != 0 ){
        perror("abrir archivo csv");
        cleanup_ipc_and_exit(1);
    }
    /* si archivo nuevo, agregamos cabecera: chequeo simple (posible mejora: stat) */
    fseek(csv, 0, SEEK_END);
    if(ftell(csv) == 0){
        if(fprintf(csv, "ID,PRODUCTOR,NOMBRE,STOCK,PRECIO\n") < 0){
            perror("fprintf cabecera");
            fclose(csv);
            cleanup_ipc_and_exit(1);
        }
        fflush(csv);
    }

    /* instalar handlers */
    struct sigaction sa_int = {0};
    sa_int.sa_handler = sigint_handler;
    sa_int.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_int, NULL);
    sigaction(SIGTERM, &sa_int, NULL);

    struct sigaction sa_chld = {0};
    sa_chld.sa_handler = sigchld_handler;
    sa_chld.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    sigaction(SIGCHLD, &sa_chld, NULL);

    /* fork productores */
    child_pids = calloc(cantProcs, sizeof(pid_t));
    if(!child_pids){ perror("calloc child_pids"); cleanup_ipc_and_exit(1); }
    for(int i=0;i<cantProcs;i++){
        pid_t pid = fork();
        if(pid < 0){
            perror("fork");
            cleanup_ipc_and_exit(1);
        }
        if(pid == 0){
            int rc = productor_func(i);
            _exit(rc);
        } else {
            child_pids[i] = pid;
        }
    }

    /* bucle coordinador: espera solicitudes y asigna bloques, además consume buffer y escribe CSV */
    int escritos = 0;
    int active_producers = cantProcs;
    while(escritos < totalIds){
        if(terminate_signal){
            fprintf(stderr,"Señal de terminación recibida.\n");
            break;
        }

        /* procesar hijos muertos */
        if(child_died){
            child_died = 0;
            int status;
            pid_t pid;
            while((pid = waitpid(-1, &status, WNOHANG)) > 0){
                for(int j=0;j<cantProcs;j++){
                    if(child_pids[j] == pid){
                        child_pids[j] = -1;
                        active_producers--;
                        fprintf(stderr,"Productor (pid=%d) finalizó. Productores activos: %d\n", pid, active_producers);
                        /* si tenía bloque asignado, reciclarlo */
                        if( sem_wait_retry(semIDs) == -1 ){ perror("sem_wait semIDs"); }
                        if(ctrl->bloques[j].asignado){
                            /* push a recycle */
                            if(ctrl->recycle_count < MAX_RECYCLE){
                                ctrl->recycle[ctrl->recycle_count].start = ctrl->bloques[j].start;
                                ctrl->recycle[ctrl->recycle_count].cantidad = ctrl->bloques[j].cantidad;
                                ctrl->recycle_count++;
                                ctrl->remaining += ctrl->bloques[j].cantidad;
                                fprintf(stderr,"Reciclado bloque start=%d cant=%d del productor idx=%d\n",
                                        ctrl->bloques[j].start, ctrl->bloques[j].cantidad, j);
                            } else {
                                fprintf(stderr,"WARNING: recycle lleno, no se recicla bloque.\n");
                            }
                            ctrl->bloques[j].asignado = 0;
                            ctrl->bloques[j].cantidad = 0;
                            ctrl->bloques[j].start = 0;
                        }
                        sem_post_retry(semIDs);
                        break;
                    }
                }
            }
            /* si ya no hay productores activos y no hay mensajes en buffer y no quedan solicitudes -> salir temprano */
            if(active_producers == 0){
                int sval=0;
                sem_getvalue(semMsgBuf,&sval);
                if(sval == 0){
                    fprintf(stderr,"No quedan productores activos y buffer vacío: terminando antes de completar todos los registros.\n");
                    break;
                }
            }
        }

        /* ===== servicio de solicitudes pendientes (no bloqueante) ===== */
int found_idx = -1;
/* procesar *todas* las solicitudes pendientes con trywait */
while( sem_trywait(semSolicitudes) == 0 ){
    /* buscar un productor que solicitó */
    if( sem_wait_retry(semIDs) == -1 ){ perror("sem_wait semIDs"); break; }
    found_idx = -1;
    for(int i=0;i<cantProcs;i++){
        if(ctrl->requests[i]){
            /* asignar si no tiene ya asignado */
            found_idx = i;
            ctrl->requests[i] = 0; // consumida la solicitud
            break;
        }
    }

    if(found_idx >= 0){
        /* elegir bloque (preferir recycle) */
        int use_start = 0, use_cant = 0;
        if(pop_recycle(ctrl, &use_start, &use_cant) && use_cant>0){
            if(use_cant > REGS_A_LEER){
                int assigned = REGS_A_LEER;
                ctrl->bloques[found_idx].start = use_start;
                ctrl->bloques[found_idx].cantidad = assigned;
                ctrl->bloques[found_idx].asignado = 1;
                int rest_start = use_start + assigned;
                int rest_cant = use_cant - assigned;
                if(ctrl->recycle_count < MAX_RECYCLE){
                    ctrl->recycle[ctrl->recycle_count].start = rest_start;
                    ctrl->recycle[ctrl->recycle_count].cantidad = rest_cant;
                    ctrl->recycle_count++;
                }
                ctrl->remaining -= assigned;
            } else {
                ctrl->bloques[found_idx].start = use_start;
                ctrl->bloques[found_idx].cantidad = use_cant;
                ctrl->bloques[found_idx].asignado = 1;
                ctrl->remaining -= use_cant;
            }
        } else {
            /* asignar desde next_id */
            if(ctrl->remaining <= 0){
                ctrl->bloques[found_idx].asignado = 0;
                ctrl->bloques[found_idx].cantidad = 0;
                ctrl->bloques[found_idx].start = 0;
            } else {
                int take = REGS_A_LEER;
                if(ctrl->remaining < take) take = ctrl->remaining;
                int start = ctrl->next_id;
                ctrl->next_id += take;
                ctrl->remaining -= take;
                ctrl->bloques[found_idx].start = start;
                ctrl->bloques[found_idx].cantidad = take;
                ctrl->bloques[found_idx].asignado = 1;
            }
        }
    }
    sem_post_retry(semIDs);

    /* avisar al productor asignado (si hubo) */
    if(found_idx >= 0){
        sem_post_retry( semRespuesta[found_idx] );
    }
}
/* ===== fin servicio de solicitudes pendientes ===== */

/* Ahora: consumir 1 registro del buffer (bloqueante) */
if( sem_wait(semMsgBuf) == -1 ){
    if(errno == EINTR) continue;
    perror("sem_wait semMsgBuf");
    break;
}
        /* leer buffer (primer elemento no nulo) */
        if( sem_wait_retry(semBuf) == -1 ){ perror("sem_wait semBuf"); break; }
        Registro reg_copy;
        int found = 0;
        for(int i=0;i<ELEMENTOS_BUF;i++){
            if(buf[i].id != 0){
                reg_copy = buf[i];
                buf[i].id = 0;
                found = 1;
                break;
            }
        }
        sem_post_retry(semBuf);
        sem_post_retry(semEspBuf);

        if(!found){
            /* inconsistencia: recibido semMsgBuf pero no hay registro */
            continue;
        }

        /* escribir en CSV */
        sem_wait_retry(semArchAux);
        if(escribir_registro_csv(csv, &reg_copy) != 0){
            fprintf(stderr,"Error escribiendo CSV id=%d\n", reg_copy.id);
        } else {
            escritos++;
            printf("[C] escribió ID %d (prod %d). Total escritos: %d/%d\n", reg_copy.id, reg_copy.productor_idx+1, escritos, totalIds);
        }
        fflush(csv);
        sem_post_retry(semArchAux);
    }

    /* esperar hijos y terminar */
    for(int i=0;i<cantProcs;i++){
        if(child_pids[i] > 0){
            kill(child_pids[i], SIGTERM);
            waitpid(child_pids[i], NULL, 0);
        }
    }

    fclose(csv);
    shmdt(buf);
    shmdt(ctrl);

    shmctl(shmbuf, IPC_RMID, NULL);
    shmctl(shmctrl, IPC_RMID, NULL);

    sem_close(semIDs); sem_close(semBuf); sem_close(semMsgBuf); sem_close(semEspBuf); sem_close(semArchAux); sem_close(semSolicitudes);
    sem_unlink(SEM_ID_NAME); sem_unlink(SEM_BR_NAME); sem_unlink(SEM_MB_NAME); sem_unlink(SEM_EB_NAME); sem_unlink(SEM_AU_NAME);
    sem_unlink(SEM_SOLICITUDES);
    for(int i=0;i<cantProcs;i++){
        char name[64];
        snprintf(name, sizeof(name), "/sem_res_tp_%d", i);
        sem_unlink(name);
    }

    free(child_pids);
    if(semRespuesta) { free(semRespuesta); semRespuesta = NULL; }

    printf("Coordinador finalizó. Escritos: %d\n", escritos);
    if(escritos < totalIds){
        fprintf(stderr, "ATENCIÓN: no se alcanzó la cantidad solicitada (%d). Archivados: %d\n", totalIds, escritos);
        return 2;
    }
    return 0;
}
