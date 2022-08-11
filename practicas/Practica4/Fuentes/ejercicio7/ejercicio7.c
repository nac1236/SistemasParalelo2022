#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <omp.h>

#define COORDINATOR 0

void suma_parcial(double ** m1, double **m2, double **m3, int n, int stripSize){
    double * matriz1 = (double *) * m1;
    double * matriz2 = (double *) * m2;
    double * matriz3 = (double *) * m3;

	#pragma omp for private(i,j) nowait
    for (int i=0; i<stripSize; i++) {
		for (int j=0; j<n ;j++ ) {
			matriz3[i*n+j] = (matriz1[i*n+j] + matriz2[i*n+j]); 
		}
    }
}

void multiplicacion_parcial(double ** m1, double **m2, double **m3, int n, int stripSize){
    double * matriz1 = (double *) * m1;
    double * matriz2 = (double *) * m2;
    double * matriz3 = (double *) * m3;

    /* computar multiplicacion parcial */
	#pragma omp for private(i,j,k) nowait
	for (int i=0; i<stripSize; i++) {
		for (int j=0; j<n ;j++ ) {
			matriz3[i*n+j]=0;
			for (int k=0; k<n ;k++ ) { 
				matriz3[i*n+j] += (matriz1[i*n+k]*matriz2[j*n+k]); 
			}
		}
	}
}


void inicializar_matriz(double ** m, int n){
    double * matriz = (double *) *m; 
    for (int i=0; i<n ; i++)
			for (int j=0; j<n ; j++)
				matriz[i*n+j] = 1;
}

int main(int argc, char* argv[]){
	int i, j, k, numProcs, rank, n, stripSize, check=1;
	double *a, *b, *c, *d, *e, *f, *r, *tmp1, *tmp2, *tmp3;
	MPI_Status status;
	double commTimes[4], maxCommTimes[4], minCommTimes[4], commTime, totalTime;
	int numThreads = 1, provided;

	/* Lee parametros de la linea de comando */
	if ((argc != 3) || ((n = atoi(argv[1])) <= 0) || ((numThreads = atoi(argv[2])) <= 0) ) {
	    printf("\nUsar: %s size numThreads \n  size: Dimension de las matrices y el vector\n  numThreads: Cantidad de hilos\n", argv[0]);
		exit(1);
	}

	//if (numThreads % 2 != 0){
	//	printf("\nnumThreads debe ser multiplo de 2.");
	//	exit(1);
	//}

	MPI_Init_thread(&argc,&argv, MPI_THREAD_MULTIPLE, &provided);

	MPI_Comm_size(MPI_COMM_WORLD,&numProcs);
	MPI_Comm_rank(MPI_COMM_WORLD,&rank);

	if (n % numProcs != 0) {
		printf("El tamanio de la matriz debe ser multiplo del numero de procesos.\n");
		exit(1);
	}

	// calcular porcion de cada worker
	stripSize = n / numProcs;

	// Reservar memoria
	if (rank == COORDINATOR) {
		a = (double*) malloc(sizeof(double)*n*n);
		c = (double*) malloc(sizeof(double)*n*n);
        e = (double*) malloc(sizeof(double)*n*n);
        r = (double*) malloc(sizeof(double)*n*n);
        tmp1 = (double*) malloc(sizeof(double)*n*n);
        tmp2 = (double*) malloc(sizeof(double)*n*n);
        tmp3 = (double*) malloc(sizeof(double)*n*n);
	}
	else  {
		a = (double*) malloc(sizeof(double)*n*stripSize);
		c = (double*) malloc(sizeof(double)*n*stripSize);
        e = (double*) malloc(sizeof(double)*n*stripSize);
        r = (double*) malloc(sizeof(double)*n*stripSize);
        tmp1 = (double*) malloc(sizeof(double)*n*stripSize);
        tmp2 = (double*) malloc(sizeof(double)*n*stripSize);
        tmp3 = (double*) malloc(sizeof(double)*n*stripSize);
	}
	
	b = (double*) malloc(sizeof(double)*n*n);
    d = (double*) malloc(sizeof(double)*n*n);
    f = (double*) malloc(sizeof(double)*n*n);


	// inicializar datos
	if (rank == COORDINATOR) {
		inicializar_matriz(&a, n);
		inicializar_matriz(&b, n);
        inicializar_matriz(&c, n);
        inicializar_matriz(&d, n);
        inicializar_matriz(&e, n);
        inicializar_matriz(&f, n);
	}

	MPI_Barrier(MPI_COMM_WORLD);

	commTimes[0] = MPI_Wtime();

	/* distribuir datos*/
	MPI_Scatter(a, n * stripSize, MPI_DOUBLE, a, n * stripSize, MPI_DOUBLE, COORDINATOR, MPI_COMM_WORLD);
	MPI_Bcast(b, n*n, MPI_DOUBLE, COORDINATOR, MPI_COMM_WORLD);

    MPI_Scatter(c, n * stripSize, MPI_DOUBLE, c, n * stripSize, MPI_DOUBLE, COORDINATOR, MPI_COMM_WORLD);
	MPI_Bcast(d, n*n, MPI_DOUBLE, COORDINATOR, MPI_COMM_WORLD);

    MPI_Scatter(e, n * stripSize, MPI_DOUBLE, e, n * stripSize, MPI_DOUBLE, COORDINATOR, MPI_COMM_WORLD);
	MPI_Bcast(f, n*n, MPI_DOUBLE, COORDINATOR, MPI_COMM_WORLD);

	commTimes[1] = MPI_Wtime();

	/* computar multiplicaciones parciales */

	#pragma omp parallel num_threads(numThreads)
	{
    	multiplicacion_parcial(&a, &b, &tmp1, n, stripSize);
    	multiplicacion_parcial(&c, &d, &tmp2, n, stripSize);
    	multiplicacion_parcial(&e, &f, &tmp3, n, stripSize);
    	suma_parcial(&tmp1, &tmp2, &a, n, stripSize);
    	suma_parcial(&a, &tmp3, &r, n, stripSize);
	}

	commTimes[2] = MPI_Wtime();

	// recolectar resultados parciales
    MPI_Gather(r, n * stripSize, MPI_DOUBLE, r, n * stripSize, MPI_DOUBLE, COORDINATOR, MPI_COMM_WORLD);

	commTimes[3] = MPI_Wtime();

	MPI_Reduce(commTimes, minCommTimes, 4, MPI_DOUBLE, MPI_MIN, COORDINATOR, MPI_COMM_WORLD);
	MPI_Reduce(commTimes, maxCommTimes, 4, MPI_DOUBLE, MPI_MAX, COORDINATOR, MPI_COMM_WORLD);

	MPI_Finalize();


	if (rank==COORDINATOR) {

		// Check results
		for(i=0;i<n;i++)
			for(j=0;j<n;j++){
				check=check&&(r[i*n+j]==(n * 3));
                if(!check){
                    printf("La posicion %d,%d tiene el valor %d\n", i , j, r[i*n+j]);
                }
            }

		if(check){
			printf("Multiplicacion de matrices resultado correcto\n");
		}else{
			printf("Multiplicacion de matrices resultado erroneo\n");
		}
		
		totalTime = maxCommTimes[3] - minCommTimes[0];
		commTime = (maxCommTimes[1] - minCommTimes[0]) + (maxCommTimes[3] - minCommTimes[2]);		

		printf("Multiplicacion de matrices (N=%d)\tTiempo total=%lf\tTiempo comunicacion=%lf\n",n,totalTime,commTime);
	}
	
	free(a);
	free(b);
	free(c);

	return 0;
}