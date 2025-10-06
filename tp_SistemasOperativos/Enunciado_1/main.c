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
#include "registro.h"

typedef struct {
    int next_id;    // próximo ID a asignar
    int remaining;  // cuántos IDs faltan por asignar
} Control;

/* semáforos nombrados */
const char *SEM_ID_NAME  = "/sem_ID_tp";
const char *SEM_BR_NAME  = "/sem_BR_tp";
const char *SEM_MB_NAME  = "/sem_MB_tp";
const char *SEM_EB_NAME  = "/sem_EB_tp";
const char *SEM_AU_NAME  = "/sem_AU_tp";

int shmbuf = -1, shmctrl = -1;
sem_t *semIDs=NULL, *semBuf=NULL, *semMsgBuf=NULL, *semEspBuf=NULL, *semArchAux=NULL;
pid_t *child_pids = NULL;
int cantProcs_glob = 0;
int totalIds_glob = 0;
volatile sig_atomic_t child_died = 0;
volatile sig_atomic_t terminate_signal = 0;

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

    sem_unlink(SEM_ID_NAME);
    sem_unlink(SEM_BR_NAME);
    sem_unlink(SEM_MB_NAME);
    sem_unlink(SEM_EB_NAME);
    sem_unlink(SEM_AU_NAME);

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

int productor_func(int idx){
    // abrir semáforos (no crear)
    semIDs     = sem_open(SEM_ID_NAME, 0);
    semBuf     = sem_open(SEM_BR_NAME, 0);
    semMsgBuf  = sem_open(SEM_MB_NAME, 0);
    semEspBuf  = sem_open(SEM_EB_NAME, 0);
    semArchAux = sem_open(SEM_AU_NAME, 0);

    if(semIDs==SEM_FAILED || semBuf==SEM_FAILED || semMsgBuf==SEM_FAILED || semEspBuf==SEM_FAILED || semArchAux==SEM_FAILED){
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

    srand(time(NULL) ^ getpid());

    while(1){
        // pedir bloque de IDs
        sem_wait(semIDs);
        if(ctrl->remaining <= 0){
            sem_post(semIDs);
            break;
        }
        int take = REGS_A_LEER;
        if(ctrl->remaining < take) take = ctrl->remaining;
        int start = ctrl->next_id;
        ctrl->next_id += take;
        ctrl->remaining -= take;
        sem_post(semIDs);

        for(int k=0;k<take;k++){
            int id_actual = start + k;
            Registro r = generar_registro_aleatorio(id_actual, idx);

            // poner en buffer
            sem_wait(semEspBuf);
            sem_wait(semBuf);
            int placed = 0;
            for(int i=0;i<ELEMENTOS_BUF;i++){
                if(buf[i].id == 0){
                    buf[i] = r;
                    placed = 1;
                    break;
                }
            }
            sem_post(semBuf);
            if(!placed){
                // por seguridad: devolver espacio y reintentar
                sem_post(semEspBuf);
                usleep(1000);
                k--; // reintentar mismo id
                continue;
            }
            sem_post(semMsgBuf);
            printf("[P%d] puso ID %d en buffer\n", idx+1, id_actual);
        }
    }

    shmdt(buf);
    shmdt(ctrl);
    sem_close(semIDs);
    sem_close(semBuf);
    sem_close(semMsgBuf);
    sem_close(semEspBuf);
    sem_close(semArchAux);
    return 0;
}

int main(int argc, char *argv[]){
    if(argc != 3){
        fprintf(stderr, "Uso: %s <cant_productores> <cant_registros_totales>\n", argv[0]);
        return 1;
    }
    int cantProcs = atoi(argv[1]);
    int totalIds  = atoi(argv[2]);
    if(cantProcs <= 0 || totalIds <= 0){
        fprintf(stderr,"Ambos parámetros deben ser enteros positivos\n");
        return 1;
    }
    cantProcs_glob = cantProcs;
    totalIds_glob = totalIds;

    /* si quedan sems antiguos, borrarlos */
    sem_unlink(SEM_ID_NAME);
    sem_unlink(SEM_BR_NAME);
    sem_unlink(SEM_MB_NAME);
    sem_unlink(SEM_EB_NAME);
    sem_unlink(SEM_AU_NAME);

    semIDs     = sem_open(SEM_ID_NAME, O_CREAT|O_EXCL, 0644, 1);
    semBuf     = sem_open(SEM_BR_NAME, O_CREAT|O_EXCL, 0644, 1);
    semMsgBuf  = sem_open(SEM_MB_NAME, O_CREAT|O_EXCL, 0644, 0);
    semEspBuf  = sem_open(SEM_EB_NAME, O_CREAT|O_EXCL, 0644, ELEMENTOS_BUF);
    semArchAux = sem_open(SEM_AU_NAME, O_CREAT|O_EXCL, 0644, 1);
    if(semIDs==SEM_FAILED || semBuf==SEM_FAILED || semMsgBuf==SEM_FAILED || semEspBuf==SEM_FAILED || semArchAux==SEM_FAILED){
        perror("sem_open");
        cleanup_ipc_and_exit(1);
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

    // inicializar buffer y control
    for(int i=0;i<ELEMENTOS_BUF;i++) buf[i].id = 0;
    ctrl->next_id = 1;
    ctrl->remaining = totalIds;

    // preparar archivo CSV y escribir cabecera
    FILE *csv = fopen("registros.csv","w");
    if(!csv){
        perror("fopen");
        cleanup_ipc_and_exit(1);
    }
    fprintf(csv, "ID,PRODUCTOR,NOMBRE,STOCK,PRECIO\n");
    fflush(csv);

    // instalar handlers
    struct sigaction sa_int = {0};
    sa_int.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa_int, NULL);
    sigaction(SIGTERM, &sa_int, NULL);

    struct sigaction sa_chld = {0};
    sa_chld.sa_handler = sigchld_handler;
    sa_chld.sa_flags = SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    // fork productores (en este mismo binario)
    child_pids = calloc(cantProcs, sizeof(pid_t));
    for(int i=0;i<cantProcs;i++){
        pid_t pid = fork();
        if(pid < 0){
            perror("fork");
            // terminar el resto
            cleanup_ipc_and_exit(1);
        }
        if(pid == 0){
            // hijo ejecuta la función productora y sale
            int rc = productor_func(i);
            _exit(rc);
        } else {
            child_pids[i] = pid;
        }
    }

    // bucle coordinador (consumidor)
    int escritos = 0;
    int active_producers = cantProcs;
    while(escritos < totalIds){
        if(terminate_signal){
            fprintf(stderr, "Señal de terminación recibida. Saliendo.\n");
            break;
        }

        // manejar hijos muertos
        if(child_died){
            child_died = 0;
            int status;
            pid_t pid;
            while((pid = waitpid(-1, &status, WNOHANG)) > 0){
                // buscar en lista
                for(int j=0;j<cantProcs;j++){
                    if(child_pids[j] == pid){
                        child_pids[j] = -1;
                        active_producers--;
                        fprintf(stderr, "Productor (pid=%d) finalizó inesperadamente. status=%d. Productores activos: %d\n", pid, status, active_producers);
                        break;
                    }
                }
            }
            // si ya no hay productores activos y no hay mensajes en buffer -> salir
            if(active_producers == 0){
                // comprobar semMsgBuf
                int sval;
                sem_getvalue(semMsgBuf, &sval);
                if(sval == 0){
                    fprintf(stderr, "No quedan productores activos y no hay mensajes: terminando antes de completar todos los registros.\n");
                    break;
                }
            }
        }

        // esperar mensaje
        if( sem_wait(semMsgBuf) == -1 ){
            if(errno == EINTR) continue;
            perror("sem_wait semMsgBuf");
            break;
        }

        // leer buffer
        sem_wait(semBuf);
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
        sem_post(semBuf);
        sem_post(semEspBuf);

        if(!found){
            // inconsistencia: habíamos recibido semMsgBuf pero no había registro
            continue;
        }

        // escribir en CSV
        sem_wait(semArchAux);
        if(escribir_registro_csv(csv, &reg_copy) != 0){
            fprintf(stderr,"Error escribiendo CSV id=%d\n", reg_copy.id);
        } else {
            escritos++;
            printf("[C] escribió ID %d (prod %d). Total escritos: %d/%d\n", reg_copy.id, reg_copy.productor_idx+1, escritos, totalIds);
        }
        fflush(csv);
        sem_post(semArchAux);
    }

    // esperar hijos (limpio)
    for(int i=0;i<cantProcs;i++){
        if(child_pids[i] > 0){
            kill(child_pids[i], SIGTERM);
            waitpid(child_pids[i], NULL, 0);
        }
    }

    // cerrar y limpiar
    fclose(csv);
    shmdt(buf);
    shmdt(ctrl);

    shmctl(shmbuf, IPC_RMID, NULL);
    shmctl(shmctrl, IPC_RMID, NULL);

    sem_close(semIDs); sem_close(semBuf); sem_close(semMsgBuf); sem_close(semEspBuf); sem_close(semArchAux);
    sem_unlink(SEM_ID_NAME); sem_unlink(SEM_BR_NAME); sem_unlink(SEM_MB_NAME); sem_unlink(SEM_EB_NAME); sem_unlink(SEM_AU_NAME);

    free(child_pids);
    printf("Coordinador finalizó. Escritos: %d\n", escritos);
    if(escritos < totalIds){
        fprintf(stderr, "ATENCIÓN: no se alcanzó la cantidad solicitada (%d). Archivados: %d\n", totalIds, escritos);
        return 2;
    }
    return 0;
}

