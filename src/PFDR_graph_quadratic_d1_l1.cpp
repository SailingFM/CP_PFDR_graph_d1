/*==================================================================
 * Hugo Raguet 2016
 *================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#ifdef _OPENMP
    #include <omp.h>
    /* rough minimum number of operations per thread */
    #define MIN_OPS_PER_THREADS 1000
#endif
#ifdef MEX
    #include "mex.h"
    #define FLUSH mexEvalString("drawnow expose")
#else
    #define FLUSH fflush(stdout)
#endif
#include "../include/PFDR_graph_quadratic_d1_l1.hpp"

/* constants of the correct type */
#define ZERO ((real) 0.)
#define ONE ((real) 1.)
#define TWO ((real) 2.)
#define HALF ((real) 0.5)
#define ALMOST_TWO ((real) 1.9)
#define HUNDREDTH ((real) 0.01)

/* nops is a rough estimation of the total number of operations 
 * max_threads is the maximum number of jobs which can be performed in 
 * parallel */
static int compute_num_threads(const int nops, const int max_threads)
{
#ifdef _OPENMP
    const int m = (omp_get_num_procs() < max_threads) ?
        omp_get_num_procs() : max_threads;
    int n = 1 + (nops - MIN_OPS_PER_THREADS)/MIN_OPS_PER_THREADS;
    return (n < m) ? n : m;
#else
    return 1;
#endif
}

template<typename real>
static void print_progress(char *msg, const int it, const int itMax, \
                           const real dif, const real difTol, const real difRcd)
{
    int k = 0;
    while (msg[k++] != '\0'){ printf("\b"); }
    sprintf(msg, "iteration %d (max. %d)\n", it, itMax);
    if (difTol > ZERO || difRcd > ZERO){
        sprintf(msg, "%siterate evolution %g (recond. %g; tol. %g)\n", msg, dif, difRcd, difTol);
    }
    printf("%s", msg);
    FLUSH;
}

template<typename real>
static void preconditioning(const int V, const int E, const int N, \
    const real *XY, const real *A, const int *Eu, const int *Ev, \
    const real *La_d1, const real *La_l1, const Lipschtype Ltype, const real *L, \
    real *Ga, const real *P, real *Zu, real *Zv, real *Wu, real *Wv, \
    real *W_d1u, real *W_d1v, real *Th_d1, real *Th_l1, const real rho, const real condMin)
/* 23 arguments 
 * for initialization:
 *      XY is Y, the observation
 *      P is NULL
 *      Zu, Zv are not used
 * for reconditioning:
 *      XY is X, the current iterate
 *      P contains the gradient of the smooth part of the functional
 *      Zu, Zv are the current auxiliary variables */
{
    /**  control the number of threads with Open MP  **/
    const int ntV = compute_num_threads(V, V);
    const int ntE = compute_num_threads(E, E);
    const int ntVN = compute_num_threads(V*N, V);

    /**  initialize general variables  **/
    int u, v, e, n; /* vertices, edges, indices */
    real a, b, c, d; /* general purpose temporary real scalars */
    const real *Av; /* columns of A */
    real *Aux; /* auxiliary array of length V */
    if (La_l1 == NULL){
        Aux = (real*) malloc(V*sizeof(real)); 
    }else{ /* Th_l1 can be used as temporary variable */
        Aux = Th_l1;
    }

    if (P != NULL){ /**  get the auxiliary subgradients  **/
        /* P stores the gradient */
        #pragma omp parallel for private(e, u, v) schedule(static) \
            num_threads(ntE)
        for (e = 0; e < E; e++){
            u = Eu[e];
            v = Ev[e];
            Zu[e] = (Wu[e]/Ga[u])*(XY[u] - Ga[u]*P[u] - Zu[e]);
            Zv[e] = (Wv[e]/Ga[v])*(XY[v] - Ga[v]*P[v] - Zv[e]);
        }
    }

    /**  initialize hessian with diagonal of (A^t A)  **/
    if (N > 0){ /* direct matricial case, compute diagonal of (A^t A) */
        #pragma omp parallel for private(v, Av, n, a) schedule(static) \
            num_threads(ntVN)
        for (v = 0; v < V; v++){
            Av = A + N*v;
            a = ZERO;
            for (n = 0; n < N; n++){ a += Av[n]*Av[n]; }
            Ga[v] = a;
        }
    }else if (N < 0){ /* premultiplied by A^t */
        #pragma omp parallel for private(v) schedule(static) num_threads(ntV)
        for (v = 0; v < V; v++){ Ga[v] = A[(V+1)*v]; }
    }else{ /* diagonal case */
        if (A != NULL){
            #pragma omp parallel for private(v) schedule(static) \
                num_threads(ntV)
            for (v = 0; v < V; v++){ Ga[v] = A[v]; }
        }else{ /* identity matrix */
            for (v = 0; v < V; v++){ Ga[v] = ONE; }
        }
    }
 
    /**  compute average amplitude from nonzero coefficients  **/
    if (P == NULL){ /* compute per-coordinate (pseudo-)inverse */
        if (N > 0){ /* direct matricial case, Pv = <Av, Y>/||Av||^2 */
            #pragma omp parallel for private(v, Av, a, n) schedule(static) \
                num_threads(ntVN)
            for (v = 0; v < V; v++){
                Av = A + N*v;
                a = ZERO;
                for (n = 0; n < N; n++){ a += Av[n]*XY[n]; }
                Aux[v] = a/Ga[v];
            }
        }else{ /* premultiplied by A^t */
            #pragma omp parallel for private(v, a) schedule(static) \
                num_threads(ntV)
            for (v = 0; v < V; v++){
                a = Ga[v];
                Aux[v] = (a > ZERO) ? XY[v]/a : ZERO;
            }
        }
    }
    const real *X = (P == NULL) ? Aux : XY;
    c = ZERO;
    n = 0;
    #pragma omp parallel for private(v, a) reduction(+:c, n) \
        schedule(static) num_threads(ntV)
    for (v = 0; v < V; v++){
        a = X[v];
        if (a > ZERO){ c += a; n++; }
        else if (a < ZERO){ c -= a; n++; }
    }
    c = (P == NULL) ? ((real) n)/c : c/((real) n);

    /**  d1 contribution and splitting weights  **/
    for (v = 0; v < V; v++){ Aux[v] = ZERO; } 
    /* this task cannot be easily parallelized */
    if (P == NULL){ /* first preconditionning */
        for (e = 0; e < E; e++){
            a = c*La_d1[e];
            Aux[Eu[e]] += a;
            Aux[Ev[e]] += a;
            Wu[e] = a;
            Wv[e] = a;
        }
    }else{ /* reconditioning */
        for (e = 0; e < E; e++){
            u = Eu[e];
            v = Ev[e];
            /* finite differences and amplitudes */
            a = XY[u];
            b = XY[v];
            d = a - b;
            /* absolute value */
            if (a < ZERO){ a = -a; }
            if (b < ZERO){ b = -b; }
            if (d < ZERO){ d = -d; }
            /* greatest amplitude */
            if (a < b){ a = b; }
            if (a < c){ a = c; }
            /* stability of the preconditioning */
            a *= condMin;
            if (d < a){ d = a; }
            /* actual preconditioning */
            a = La_d1[e]/d;
            Aux[u] += a;
            Aux[v] += a;
            Wu[e] = a;
            Wv[e] = a;
        }
    }
    /* add the contribution to the Hessian */
    #pragma omp parallel for private(v) schedule(static) num_threads(ntV)
    for (v = 0; v < V; v++){ Ga[v] += Aux[v]; }
    /* make splitting weights sum to unity */
    #pragma omp parallel for private(v) schedule(static) num_threads(ntV)
    for (v = 0; v < V; v++){ Aux[v] = ONE/Aux[v]; }
    #pragma omp parallel for private(e) schedule(static) num_threads(ntE)
    for (e = 0; e < E; e++){
        Wu[e] *= Aux[Eu[e]];
        Wv[e] *= Aux[Ev[e]];
    }

    if (La_l1 != NULL){ /**  l1 contribution  **/
        if (P == NULL){
            #pragma omp parallel for private(v) schedule(static) num_threads(ntV)
            for (v = 0; v < V; v++){ Ga[v] += c*La_l1[v]; }
        }else{
            c *= condMin;
            #pragma omp parallel for private(v, d) schedule(static) num_threads(ntV)
            for (v = 0; v < V; v++){
                d = XY[v];
                if (d < ZERO){ d = -d; }
                if (d < c){ d = c; }
                Ga[v] += La_l1[v]/d;
            }
        }
    }

    /**  inverse the approximate of the Hessian  **/
    #pragma omp parallel for private(v) schedule(static) num_threads(ntV)
    for (v = 0; v < V; v++){ Ga[v] = ONE/Ga[v]; }
    /**  convergence condition on the metric  **/
    a = ALMOST_TWO*(TWO - rho);
    if (Ltype == SCAL || L == NULL){
        if (L != NULL){ a /= (*L); }
        #pragma omp parallel for private(v) schedule(static) num_threads(ntV)
        for (v = 0; v < V; v++){ if (Ga[v] > a){ Ga[v] = a; } }
    }else{ /* (Ltype == DIAG) */
        #pragma omp parallel for private(v, b) schedule(static) \
            num_threads(ntV)
        for (v = 0; v < V; v++){
            if (L[v] > ZERO){
                b = a/L[v];
                if (Ga[v] > b){ Ga[v] = b; }
            }
        }
    }

    if (P != NULL){ /**  update auxiliary variables  **/
        #pragma omp parallel for private(e, u, v) schedule(static) \
            num_threads(ntE)
        for (e = 0; e < E; e++){
            u = Eu[e];
            v = Ev[e];
            Zu[e] = XY[u] - Ga[u]*(P[u] + Zu[e]/Wu[e]);
            Zv[e] = XY[v] - Ga[v]*(P[v] + Zv[e]/Wv[e]);
        }
    }

    /**  precompute some quantities  **/
    #pragma omp parallel for private(e, a) schedule(static) num_threads(ntE)
    for (e = 0; e < E; e++){
        W_d1u[e] = Wu[e]/Ga[Eu[e]];
        W_d1v[e] = Wv[e]/Ga[Ev[e]];
        a = W_d1u[e] + W_d1v[e];
        Th_d1[e] = La_d1[e]*a/(W_d1u[e]*W_d1v[e]);
        W_d1u[e] /= a;
        W_d1v[e] /= a;
    }
    if (La_l1 != NULL){
        #pragma omp parallel for private(v) schedule(static) num_threads(ntV)
        for (v = 0; v < V; v++){ Th_l1[v] = Ga[v]*La_l1[v]; }
    }else{
        free(Aux);
    }
}

template <typename real>
void PFDR_graph_quadratic_d1_l1(const int V, const int E, const int N, \
    real *X, const real *Y, const real *A, const int *Eu, const int *Ev, \
    const real *La_d1, const real *La_l1, const int positivity, \
    const Lipschtype Ltype, const real *L, const real rho, const real condMin, \
    real difRcd, const real difTol, const int itMax, int *it, \
    real *Obj, real *Dif, const int verbose)
/* 22 arguments */
{
    /***  initialize general variables  ***/
    if (verbose){ printf("Initializing constants and variables... "); FLUSH; }
    int u, v, e, n, i; /* vertices, edges, indices */
    real a, b, c; /* general purpose temporary real scalars */
    const real *Av; /* columns of A */

    /**  precision  **/
    switch (sizeof(real)){
        case sizeof(float) : a = (real) FLT_EPSILON; break;
        case sizeof(double) : a = (real) DBL_EPSILON; break;
        case sizeof(long double) : a = (real) LDBL_EPSILON; break;
        default : a = (real) FLT_EPSILON;
    }
    const real eps = (ZERO < difTol && difTol < a) ? difTol : a;

    /**  control the number of threads with Open MP  **/
    const int ntV = compute_num_threads(V, V);
    const int ntE = compute_num_threads(E, E);
    const int ntN = compute_num_threads(N, N);
    const int ntNV = compute_num_threads(N*V, N);
    const int ntVN = compute_num_threads(V*N, V);
    const int ntVV = compute_num_threads(V*V, V);

    /**  allocates general purpose arrays  **/
    real *Ga = (real*) malloc(V*sizeof(real)); /* descent metric */
    /* auxiliary variables for generalized forward-backward */
    real *Zu = (real*) malloc(E*sizeof(real));
    real *Zv = (real*) malloc(E*sizeof(real));
    /* splitting weights for generalized forward-backward */
    real *Wu = (real*) malloc(E*sizeof(real));
    real *Wv = (real*) malloc(E*sizeof(real));
    /* store some quantities */
    real *W_d1u = (real*) malloc(E*sizeof(real));
    real *W_d1v = (real*) malloc(E*sizeof(real));
    real *Th_d1 = (real*) malloc(E*sizeof(real));
    real *Th_l1 = NULL;
    if (La_l1 != NULL){ Th_l1 = (real*) malloc(V*sizeof(real)); }
    real *P = (real*) malloc(V*sizeof(real)); /* store forward step and gradient */
    real *R = NULL; /* store residual in direct matrix mode */
    if (N > 0){ R = (real*) malloc(N*sizeof(real)); }
    /* initialize x *//* assumed already initialized */
    /* initialize, for all i, z_i = x */
    for (e = 0; e < E; e++){
        Zu[e] = X[Eu[e]];
        Zv[e] = X[Ev[e]];
    }
    if (verbose){ printf("done.\n"); FLUSH; }

    /***  preconditioning  ***/
    if (verbose){ printf("Preconditioning... "); FLUSH; }
    preconditioning<real>(V, E, N, Y, A, Eu, Ev, La_d1, La_l1, \
                          Ltype, L, Ga, NULL, NULL, NULL, Wu, Wv, W_d1u, \
                          W_d1v, Th_d1, Th_l1, rho, condMin);
    if (verbose){ printf("done.\n"); FLUSH; }

    /***  forward-Douglas-Rachford  ***/
    if (verbose){ printf("Preconditioned forward-Douglas-Rachford algorithm\n"); FLUSH; }
    /* initialize */
    int itMsg, it_ = 0;
    real dif, *X_ = NULL; /* store last iterate */
    const real difTol2 = difTol*difTol;
    real difRcd2 = difRcd*difRcd;
    char msg[256];
    dif = (difTol2 > difRcd2) ? difTol2 : difRcd2;
    if (difTol > ZERO || difRcd > ZERO || Dif != NULL){
        X_ = (real*) malloc(V*sizeof(real));
        for (v = 0; v < V; v++){ X_[v] = X[v]; }
    }
    if (verbose){
        msg[0] = '\0';
        itMsg = 0;
    }

    /***  main loop  ***/
    while (true){

        /**  apply A  **/
        if (N > 0){ /* direct matricial case, compute residual R = Y - A X */
            #pragma omp parallel for private(n, a, i, v) schedule(static) \
                num_threads(ntNV)
            for (n = 0; n < N; n++){
                a = ZERO;
                i = n;
                for (v = 0; v < V; v++){
                    a += A[i]*X[v];
                    i += N;
                }
                R[n] = Y[n] - a;
            }
        }else if (N < 0){ /* premultiplied by A^t, P = (A^t A) X */
            #pragma omp parallel for private(u, v, Av, a) schedule(static) \
                num_threads(ntVV)
            for (v = 0; v < V; v++){
                Av = A + V*v;
                a = ZERO;
                for (u = 0; u < V; u++){ a += Av[u]*X[u]; }
                P[v] = a;
            }
        }else{ /* diagonal case, P = (A^t A) X */
            if (A != NULL){ 
                #pragma omp parallel for private(v) schedule(static) \
                    num_threads(ntV)
                for (v = 0; v < V; v++){ P[v] = A[v]*X[v]; }
            }else{ /* identity matrix */
                for (v = 0; v < V; v++){ P[v] = X[v]; }
            }
        }
 
        /**  objective functional value  **/
        if (Obj != NULL){ 
            a = ZERO;
            if (N > 0){ /* direct matricial case, 1/2 ||Y - A X||^2 */
                #pragma omp parallel for private(n) reduction(+:a) \
                    schedule(static) num_threads(ntN)
                for (n = 0; n < N; n++){ a += R[n]*R[n]; }
                Obj[it_] = HALF*a;
            }else{ /* premultiplied by A^t, 1/2 <X, A^t A X> - <X, A^t Y> */
                #pragma omp parallel for private(v) reduction(+:a) \
                    schedule(static) num_threads(ntV)
                for (v = 0; v < V; v++){ a += X[v]*(HALF*P[v] - Y[v]); }
                Obj[it_] = a;
            }
            /* ||x||_{d1,La_d1} */
            a = ZERO;
            #pragma omp parallel for private(e, b) reduction(+:a) \
                schedule(static) num_threads(ntE)
            for (e = 0; e < E; e++){
                b = X[Eu[e]] - X[Ev[e]];
                if (b < ZERO){ a -= La_d1[e]*b; }
                else{ a += La_d1[e]*b; }
            }
            Obj[it_] += a;
            if (La_l1 != NULL){ /* ||x||_{l1,La_l1} */
                a = ZERO;
                #pragma omp parallel for private(v, b) reduction(+:a) \
                    schedule(static) num_threads(ntV)
                for (v = 0; v < V; v++){
                    b = X[v];
                    if (b < ZERO){ a -= La_d1[e]*b; }
                    else{ a += La_l1[v]*b; }
                }
                Obj[it_] += a;
            }
        }

        /**  stopping criteria and information  **/
        if (verbose && itMsg++ == verbose){
            print_progress<real>(msg, it_, itMax, dif, difTol2, difRcd2);
            itMsg = 1;
        }
        if (it_ == itMax || dif < difTol2){ break; }

        /**  compute gradient  **/ 
        if (N > 0){ /* direct matricial case, P = -(A^t) R */
            #pragma omp parallel for private(v, Av, a, n) schedule(static) \
                num_threads(ntVN)
            for (v = 0; v < V; v++){
                Av = A + N*v;
                a = ZERO;
                for (n = 0; n < N; n++){ a += Av[n]*R[n]; }
                P[v] = -a;
            }
        }else{ /* premultiplied by A^t, P = (A^t A) X - A^t Y */
            #pragma omp parallel for private(v) schedule(static) \
                num_threads(ntV)
            for (v = 0; v < V; v++){ P[v] -= Y[v]; }
        }

        /**  reconditioning  **/
        if (dif < difRcd2){
            if (verbose){
                print_progress<real>(msg, it_, itMax, dif, difTol2, difRcd2);
                printf("Reconditioning... ");
                FLUSH;
                msg[0] = '\0';
            }
            preconditioning<real>(V, E, N, X, A, Eu, Ev, La_d1, La_l1, \
                                  Ltype, L, Ga, P, Zu, Zv, Wu, Wv, W_d1u, \
                                  W_d1v, Th_d1, Th_l1, rho, condMin);
            difRcd2 *= HUNDREDTH;
            if (verbose){ printf("done.\n"); FLUSH; }
        }

        /**  forward step, P = 2 X - Ga grad(X) **/ 
        #pragma omp parallel for private(v) schedule(static) num_threads(ntV)
        for (v = 0; v < V; v++){ P[v] = TWO*X[v] - Ga[v]*P[v]; }

        /**  backward step on auxiliary variables (prox d1)  **/
        #pragma omp parallel for private(e, u, v, a, b) schedule(static) \
            num_threads(ntE)
        for (e = 0; e < E; e++){
            u = Eu[e];
            v = Ev[e];
            /* weighted average */
            a = W_d1u[e]*(P[u] - Zu[e]) + W_d1v[e]*(P[v] - Zv[e]);
            /* difference */
            b = (P[u] - Zu[e]) - (P[v] - Zv[e]);
            /* soft thresholding, update and relaxation */
            if (b > Th_d1[e]){
                b -= Th_d1[e];
                Zu[e] += rho*(a + W_d1v[e]*b - X[u]);
                Zv[e] += rho*(a - W_d1u[e]*b - X[v]);
            }else if (b < -Th_d1[e]){
                b += Th_d1[e];
                Zu[e] += rho*(a + W_d1v[e]*b - X[u]);
                Zv[e] += rho*(a - W_d1u[e]*b - X[v]);
            }else{
                Zu[e] += rho*(a - X[u]);
                Zv[e] += rho*(a - X[v]);
            }
        }

        /**  average  **/
        for (v = 0; v < V; v++){ X[v] = ZERO; }
        /* this task cannot be easily parallelized */
        for (e = 0; e < E; e++){
            X[Eu[e]] += Wu[e]*Zu[e];
            X[Ev[e]] += Wv[e]*Zv[e];
        }

        /**  backward step on iterate (prox l1 + positivity)  **/
        if (La_l1 != NULL){
            #pragma omp parallel for private(v) schedule(static) \
                num_threads(ntV)
            for (v = 0; v < V; v++){
                if (X[v] > Th_l1[v]){ X[v] -= Th_l1[v]; }
                else if (!positivity && (X[v] < -Th_l1[v])){ X[v] += Th_l1[v]; }
                else{ X[v] = ZERO; }
            }
        }else if (positivity){
            #pragma omp parallel for private(v) schedule(static) \
                num_threads(ntV)
            for (v = 0; v < V; v++){ if (X[v] < ZERO){ X[v] = ZERO; } }
        }
 
        /**  iterate evolution  **/
        if (difTol > ZERO || difRcd > ZERO || Dif != NULL){
            dif = ZERO;
            c = ZERO;
            #pragma omp parallel for private(v, a, b) reduction(+:dif, c) \
                schedule(static) num_threads(ntV)
            for (v = 0; v < V; v++){
                a = X[v];
                b = X_[v] - a;
                dif += b*b;
                c += a*a;
                X_[v] = a;
            }
            dif = (c > eps) ? dif/c : dif/eps;
            if (Dif != NULL){ Dif[it_] = dif; }
        }

        it_++;
    } /* endwhile (true) */

    /* final information */
    *it = it_;
    if (verbose){
        print_progress<real>(msg, it_, itMax, dif, difTol2, difRcd2);
        FLUSH;
    }

    /* free stuff */
    free(Zu);
    free(Zv);
    free(Wu);
    free(Wv);
    free(W_d1u);
    free(W_d1v);
    free(Th_d1);
    free(Th_l1);
    free(P);
    free(R);
    free(X_);
}

/* instantiate for compilation */
template void PFDR_graph_quadratic_d1_l1<float>(const int, const int, const int, \
        float*, const float*, const float*, const int*, const int*, \
        const float*, const float*, const int, \
        const Lipschtype, const float*, const float, const float, \
        float, const float, const int, int*, float*, float*, const int);

template void PFDR_graph_quadratic_d1_l1<double>(const int, const int, const int, \
        double*, const double*, const double*, const int*, const int*, \
        const double*, const double*, const int, \
        const Lipschtype, const double*, const double, const double, \
        double, const double, const int, int*, double*, double*, const int);
