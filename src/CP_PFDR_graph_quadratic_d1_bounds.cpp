/*=============================================================================
 * Hugo Raguet 2016
 *===========================================================================*/
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <float.h>
#ifdef _OPENMP
    #include <omp.h>
    /* rough minimum number of operations per thread */
    #define MIN_OPS_PER_THREADS 1000
#endif
#ifdef MEX
    #include "mex.h"
    #define FLUSH mexEvalString("drawnow expose")
    #define CALLOC mxCalloc
    #define FREE mxFree
#else
    #define FLUSH fflush(stdout)
    #define CALLOC calloc
    #define FREE free
#endif
#include "../include/graph.hpp" /* Boykov-Kolmogorov graph class */
#include "../include/operator_norm_matrix.hpp"
#include "../include/PFDR_graph_quadratic_d1_bounds.hpp"

/* constants of the correct type */
#define ZERO ((real) 0.)
#define ONE ((real) 1.)
#define TWO ((real) 2.)
#define HALF ((real) 0.5)
#define TRUE ((uint8_t) 1)
#define FALSE ((uint8_t) 0)

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
static void print_progress(int it, int itMax, double t, \
    const int rV, const int rE, const real dif, const real difTol)
{
    printf("\n\tCut pursuit iteration %d (max. %d)\n", it, itMax);
    if (difTol > ZERO){
        printf("\titerate evolution %g (tol. %g)\n", dif, difTol);
    }
    printf("\t%d connected component(s), %d reduced edge(s)\n", rV, rE);
    if (t > 0.){ printf("\telapsed time %.1f s\n", t); }
    FLUSH;
}

template<typename real>
static void initialize(const int V, const int E, const int N, \
    int *rV, int *Cv, real **rX, const real *Y, const real *A, \
    const int *Eu, const int *Ev, const real min, const real max, \
    Graph<real, real, real> **G, int **Vc, int **rVc, real **R, real *obj)
{
    /**  initialize general variables  **/
    int u, v, e, n, i; /* vertices, edges, indices */
    real a; /* general purpose temporary real scalar */
    real rY, rAA;
    const real *Av; /* columns of A or AtA */
    real *rA; /* reduced matrix */

    /** control the number of threads with Open MP **/
    const int ntN = compute_num_threads(N, N);
    const int ntV = compute_num_threads(V, V);
    const int ntNV = compute_num_threads(N*V, N);
    const int ntVV = compute_num_threads(V*V, V);

    /**  construct graph **/
    Graph<real, real, real> *H = new Graph<real, real, real>(V, E); 
    H->add_node(V);
    /* d1 edges */
    for (e = 0; e < E; e++){ H->add_edge(Eu[e], Ev[e], ZERO, ZERO); }
    /* source/sink edges */
    for (v = 0; v < V; v++){ H->add_tweights(v, ZERO, ZERO); }
    *G = H;

    /**  solve unidimensional quadratic + bounds problem  **/
    *rX = (real*) CALLOC(1, sizeof(real));
    rY = ZERO; /* will contain <A1|Y> */
    rAA = ZERO; /* will contain ||A1||^2 */
    if (N > 0){ /* direct matricial case */
        rA = (real*) malloc(N*sizeof(real)); /* sum the columns of A */
        #pragma omp parallel for private(n, i, v, a) schedule(static) \
            num_threads(ntNV)
        for (n = 0; n < N; n++){
            i = n;
            a = ZERO;
            for (v = 0; v < V; v++){
                a += A[i];
                i += N;
            }
            rA[n] = a;
        }
        #pragma omp parallel for private(n, a) reduction(+:rY, rAA) \
            schedule(static) num_threads(ntN)
        for (n = 0; n < N; n++){
            a = rA[n];
            rY += a*Y[n];
            rAA += a*a;
        }
    }else{ /* premultiplied by A^t */
        #pragma omp parallel for private(v) reduction(+:rY) schedule(static) \
            num_threads(ntV)
        for (v = 0; v < V; v++){ rY += Y[v]; }
        if (N < 0){ /* full matrix */
            #pragma omp parallel for private(u, v, Av) reduction(+:rAA) \
                schedule(static) num_threads(ntVV)
            for (u = 0; u < V; u++){
                Av = A + V*u;
                for (v = 0; v < V; v++){ rAA += Av[v]; }
            }
        }else if (A != NULL){ /* diagonal matrix */
            #pragma omp parallel for private(v) reduction(+:rAA) \
                schedule(static) num_threads(ntV)
            for (v = 0; v < V; v++){ rAA += A[v]; }
        }else{ /* identity matrix */
            rAA = (real) V;
        }
    }
    **rX = rY/rAA;
    /* projector on bounds over least-square solution */
    if (**rX < min){ **rX = min; }
    else if (**rX  > max){ **rX = max; }

    /**  assign every vertex to the unique component  **/
    *rV = 1;
    for (v = 0; v < V; v++){ Cv[v] = 0; }
    *Vc = (int*) malloc(V*sizeof(int));
    for (v = 0; v < V; v++){ (*Vc)[v] = v; }
    *rVc = (int*) malloc(2*sizeof(int));
    (*rVc)[0] = 0; (*rVc)[1] = V;
    
    if (N > 0){ /** direct matricial case, compute residual R = Y - A X  **/
        *R = rA; /* can be done in-place */
        a = **rX;
        #pragma omp parallel for private(n) num_threads(ntN)
        for (n = 0; n < N; n++){ (*R)[n] = Y[n] - rA[n]*a; }
    }

    /**  objective functional value  **/
    if (obj != NULL){ 
        a = ZERO;
        if (N > 0){ /* direct matricial case, 1/2 ||Y - A X||^2 */
            #pragma omp parallel for private(n) reduction(+:a) \
                schedule(static) num_threads(ntN)
            for (n = 0; n < N; n++){ a += (*R)[n]*(*R)[n]; }
            a *= HALF;
        }else{ /* premultiplied by A^t, 1/2 <X, A^t A X> - <X, A^t Y> */
            a = (**rX)*(HALF*rAA*(**rX) - rY);
        }
        *obj = a;
        /* ||x||_{d1,La_d1} = 0 */
    }
}

template <typename real> struct CPqb_Restart
{
    Graph<real, real, real> *G;
    int *Vc;
    int *rVc;
    real *R;
};

template <typename real> struct CPqb_Restart<real>*
create_CPqb_Restart(const int V, const int E, const int N, \
                    int *rV, int *Cv, real **rX, const real *Y, const real *A, \
                    const int *Eu, const int *Ev, const real min, const real max)
{
    struct CPqb_Restart<real> *CP_restart = malloc(sizeof(struct CPqb_Restart<real>));
    Graph<real, real, real> *G;
    real *Vc, *rVc, *R;
    initialize(V, E, N, rV, Cv, rX, Y, A, Eu, Ev, \
                min, max, &G, &Vc, &rVc, &R, NULL);
    CP_restart->G = G;
    CP_restart->Vc = Vc;
    CP_restart->rVc = rVc;
    CP_restart->R = R;
    return CP_restart;
}

template <typename real> void free_CPqb_Restart(struct CPqb_Restart<real> *CP_restart)
{
    delete CP_restart->G;
    free(CP_restart->Vc);
    free(CP_restart->rVc);
    free(CP_restart->R); 
    free(CP_restart);
}

template <typename real>
void CP_PFDR_graph_quadratic_d1_bounds(const int V, const int E, const int N, \
    int *rV, int *Cv, real **rX, const real *Y, const real *A, \
    const int *Eu, const int *Ev, const real *La_d1, \
    const real min, const real max, \
    const real CP_difTol, const int CP_itMax, int *CP_it, \
    const real PFDR_rho, const real PFDR_condMin, \
    const real PFDR_difRcd, const real PFDR_difTol, const int PFDR_itMax, \
    double *Time, real *Obj, real *Dif, const int verbose, \
    struct CPqb_Restart<real> *CP_restart)
/* 26 arguments */
{
    /***  initialize main graph and general variables  ***/
    if (verbose){
        printf("\tInitializing constants, variables, graph structure " \
               "and solution of reduced problem... ");
        FLUSH;
    }
    int s, t, u, v, w, e, ru, rv, re, n, i; /* vertices, edges, indices */
    typename Graph<real, real, real>::arc_id ee; /* pointer over edge in graph structure */
    real a, b, c, d; /* general purpose temporary real scalars */

    /**  smallest and largest values  **/
    switch (sizeof(real)){
        case sizeof(float) :
            a = (real) FLT_EPSILON;
            b = (real) HUGE_VALF;
            break;
        case sizeof(double) :
            a = (real) DBL_EPSILON;
            b = (real) HUGE_VAL;
            break;
        case sizeof(long double) :
            a = (real) LDBL_EPSILON;
            b = (real) HUGE_VALL;
            break;
        default :
            a = (real) FLT_EPSILON;
            b = (real) HUGE_VALF;
    }
    const real eps = (ZERO < CP_difTol && CP_difTol < a) ? CP_difTol : a;
    const real inf = b;

    /** monitor elapsing time **/
    double timer = 0.;
    struct timespec time0, timeIt;
    if (Time != NULL){ clock_gettime(CLOCK_MONOTONIC, &time0); }

    /** control the number of threads with Open MP **/
    const int ntN = compute_num_threads(N, N);
    const int ntV = compute_num_threads(V, V);
    const int ntE = compute_num_threads(E, E);
    const int ntNV = compute_num_threads(N*V, N);
    const int ntVV = compute_num_threads(V*V, V);
    int ntrV, ntrE, ntV_rV, ntNV_rV, ntVV_rV, ntrVrV, ntrVN, ntNrV;
    ntrV = ntrE = ntV_rV = ntNV_rV = ntVV_rV = ntrVrV = ntrVN = 1;
    ntNrV = ntN;

    /** robust parameters for subroutines **/
    /* stochastic matrix operator norm */
    const real normTol = (real) 1e-3;
    const int normItMax = 100;
    const int normNbInit = 10;

    /**  some meaningful pointers  **/
    real *R = NULL; /* residual in direct matrix mode */
    real *DfS; /* gradient of differentiable part */
    const real *Av; /* columns of A or AtA */
    real *rY, *rA, *rAA, *rAv; /* reduced observation, matrix, symmetrized, columns */
    rY = rA = rAA = NULL;
    int *rEu, *rEv, *rEc; /* for reduced edge set */
    rEu = rEv = NULL;
    int *Vc; /* list of vertices within each connected component */
    int *rVc, *rVc_; /* cumulative sum of the components sizes */
    real *rLa_d1; /* reduced penalizations */
    rLa_d1 = NULL;
    real *L; /* Lipschitz diagonal metric for reduced problem */

    /***  cut pursuit initialization  ***/
    int rV_, rE = 0; /* number of vertices and edges in reduced graph */
    Graph<real, real, real> *G;
    if (CP_restart == NULL){
        initialize(V, E, N, &rV_, Cv, rX, Y, A, Eu, Ev, \
                    min, max, &G, &Vc, &rVc, &R, Obj);
    }else{ /**  warm restart  **/
        rV_ = *rV;
        G = CP_restart->G;
        Vc = CP_restart->Vc;
        rVc = CP_restart->rVc;
        R = CP_restart->R;
        /* Cv, rX are supposed to be initialized accordingly */
    }
    if (verbose){ printf("done.\n"); FLUSH; }

    /***  cut pursuit  ***/
    /* initialize */
    int PFDR_it = PFDR_itMax, CP_it_ = 0;
    int *Cv_ = NULL; /* store last iterate partition */
    real dif, *rX_ = NULL; /* store last iterate values */
    const real CP_difTol2 = CP_difTol*CP_difTol;
    int preAt; /* flag for premultiplication by A^t */
    dif = CP_difTol2 > ONE ? CP_difTol2 : ONE;
    if (CP_difTol > ZERO || Dif != NULL){
        rX_ = (real*) malloc(rV_*sizeof(real));
        for (rv = 0; rv < rV_; rv++){ rX_[rv] = (*rX)[rv]; }
        Cv_ = (int*) malloc(V*sizeof(int));
        for (v = 0; v < V; v++){ Cv_[v] = Cv[v]; }
    }
    /***  main loop  ***/
    while (true){

        /**  elapsed time  **/
        if (Time != NULL){
            clock_gettime(CLOCK_MONOTONIC, &timeIt);
            timer = timeIt.tv_sec - time0.tv_sec;
            timer += (timeIt.tv_nsec - time0.tv_nsec)/1000000000.;
            Time[CP_it_] = timer;
        }

        /**  stopping criteria and information  **/
        if (verbose){
            print_progress<real>(CP_it_, CP_itMax, timer, rV_, rE, dif, CP_difTol2);
        }
        if (CP_it_ == CP_itMax || dif < CP_difTol2){ break; }
        
        /***  steepest cuts  ***/
        if (verbose){ printf("\tSteepest cut... "); FLUSH; }
        /**  compute gradient of quadratic term  **/ 
        DfS = (real*) malloc(V*sizeof(real));
        if (N > 0){ /* direct matricial case, DfS = -(A^t) R */
            #pragma omp parallel for private(v, Av, a, n) schedule(static) \
                num_threads(ntNV)
            for (v = 0; v < V; v++){
                Av = A + N*v;
                a = ZERO;
                for (n = 0; n < N; n++){ a += Av[n]*R[n]; }
                DfS[v] = -a;
            }
            free(R);
            R = NULL;
        }else if (N < 0){ /* premultiplied by A^t, DfS = (A^t A)*X - A^t Y  */
            #pragma omp parallel for private(s, t, u, v, rv, Av, a, b) \
                schedule(static) num_threads(ntVV)
            for (u = 0; u < V; u++){
                Av = A + V*u;
                b = ZERO;
                for (rv = 0; rv < rV_; rv++){
                    if ((*rX)[rv] == ZERO){ continue; }
                    a = ZERO;
                    /* run along the component rv */
                    for (v = Vc[s = rVc[rv]], t = rVc[rv+1]; s < t; v = Vc[++s]){
                        a += Av[v];
                    }
                    b += a*(*rX)[rv];
                }
                DfS[u] = b - Y[u];
            }
        }else{ /* diagonal case, DfS = (A^t A) X - A^t Y */
            if (A != NULL){ 
                #pragma omp parallel for private(v) schedule(static) \
                    num_threads(ntV)
                for (v = 0; v < V; v++){ DfS[v] = A[v]*(*rX)[Cv[v]] - Y[v]; }
            }else{ /* identity matrix */
                for (v = 0; v < V; v++){ DfS[v] = (*rX)[Cv[v]] - Y[v]; }
            }
        }

        /**  add the differentiable d1 contribution  **/ 
        #pragma omp parallel for private(u, v, ee, e, d) schedule(static) \
            num_threads(ntV)
        for (u = 0; u < V; u++){
            for (ee = G->nodes[u].first; ee; ee = ee->next){
                if (ee->is_active){
                    v = (int) (ee->head - G->nodes);
                    e = ((int) (ee - G->arcs))/2;
                    d = (*rX)[Cv[u]] - (*rX)[Cv[v]];
                    if (d > ZERO){
                        DfS[u] += La_d1[e];
                    }else if (d < ZERO){
                        DfS[u] -= La_d1[e];
                    }
                }
            }
        }

        w = 0; /* edge activation counter */
        if (-inf == min && max == inf){ /* differentiable case */
            /**  only one cut: directions 1_U - 1_Uc  **/
            /* set the source/sink capacities */
            #pragma omp parallel for private(v, a) schedule(static) \
                num_threads(ntV)
            for (v = 0; v < V; v++){ G->nodes[v].tr_cap = DfS[v]; }
            /* set the d1 edge capacities */
            #pragma omp parallel for private(e, i, a) schedule(static) \
                num_threads(ntE)
            for (e = 0; e < E; e++){
                i = 2*e;
                if (G->arcs[i].is_active){
                    G->arcs[i].r_cap = ZERO;
                    G->arcs[i+1].r_cap = ZERO;
                }else{
                    a = La_d1[e];
                    G->arcs[i].r_cap = a;
                    G->arcs[i+1].r_cap = a;
                }
            }
            /* find min cut and activate corresponding edges */
            G->maxflow();
            #pragma omp parallel for private(e, i) reduction(+:w) schedule(static) \
                num_threads(ntE)
            for (e = 0; e < E; e++){
                if (G->what_segment(Eu[e]) != G->what_segment(Ev[e])){
                    i = 2*e;
                    if (!G->arcs[i].is_active){
                        G->arcs[i].is_active = TRUE;
                        G->arcs[i+1].is_active = TRUE;
                        w++;
                    }
                }
            }
        }else{ /* nondifferentiable case */
            /**  first cut: directions +1_U  **/
            /* set the source/sink capacities */
            if (max < inf){
                #pragma omp parallel for private(rv, s, t, v) schedule(static) \
                    num_threads(ntV_rV)
                for (rv = 0; rv < rV_; rv++){
                    if ((*rX)[rv] == max){
                        /* run along the component rv */
                        for (v = Vc[s = rVc[rv]], t = rVc[rv+1]; s < t; v = Vc[++s]){
                            G->nodes[v].tr_cap = inf; 
                        }
                    }else{
                        /* run along the component rv */
                        for (v = Vc[s = rVc[rv]], t = rVc[rv+1]; s < t; v = Vc[++s]){
                            G->nodes[v].tr_cap = DfS[v]; 
                        }
                    }
                }
            }else{ /* lower bound changes nothing for directions +1_U */
                #pragma omp parallel for private(v) schedule(static) \
                    num_threads(ntV)
                for (v = 0; v < V; v++){ G->nodes[v].tr_cap = DfS[v]; }
            }
            /* set the d1 edge capacities */
            #pragma omp parallel for private(e, i, a) schedule(static) \
                num_threads(ntE)
            for (e = 0; e < E; e++){
                i = 2*e;
                if (G->arcs[i].is_active){
                    G->arcs[i].r_cap = ZERO;
                    G->arcs[i+1].r_cap = ZERO;
                }else{
                    a = La_d1[e];
                    G->arcs[i].r_cap = a;
                    G->arcs[i+1].r_cap = a;
                }
            }
            /* find min cut */
            G->maxflow();
            /* corresponding edges will be activated 
             * when setting d1 edge capacity for second cut */

            /**  second cut: directions -1_U  **/
            /* set the source/sink capacities */
            if (-inf < min){
                #pragma omp parallel for private(rv, s, t, v) schedule(static) \
                    num_threads(ntV_rV)
                for (rv = 0; rv < rV_; rv++){
                    if ((*rX)[rv] == min){
                        /* run along the component rv */
                        for (v = Vc[s = rVc[rv]], t = rVc[rv+1]; s < t; v = Vc[++s]){
                            G->nodes[v].tr_cap = inf; 
                        }
                    }else{
                        /* run along the component rv */
                        for (v = Vc[s = rVc[rv]], t = rVc[rv+1]; s < t; v = Vc[++s]){
                            G->nodes[v].tr_cap = -DfS[v]; 
                        }
                    }
                }
            }else{ /* upper bound changes nothing for directions -1_U */
                #pragma omp parallel for private(v) schedule(static) \
                    num_threads(ntV)
                for (v = 0; v < V; v++){ G->nodes[v].tr_cap = -DfS[v]; }
            }
            /* set the d1 edge capacities and activate edges from first cut */
            t = 0; /* auxiliary edge activation counter */
            #pragma omp parallel for private(e, i, a) reduction(+:t) \
                schedule(static) num_threads(ntE)
            for (e = 0; e < E; e++){
                i = 2*e;
                if (G->arcs[i].is_active){
                    G->arcs[i].r_cap = ZERO;
                    G->arcs[i+1].r_cap = ZERO;
                }else{
                    a = La_d1[e];
                    G->arcs[i].r_cap = a;
                    G->arcs[i+1].r_cap = a;
                    if (G->what_segment(Eu[e]) != G->what_segment(Ev[e])){
                        G->arcs[i].is_active = TRUE;
                        G->arcs[i+1].is_active = TRUE;
                        t++;
                    }
                }
            }
            w += t;
            /* find min cut and activate corresponding edges */
            G->maxflow();
            t = 0; /* auxiliary edge activation counter */
            #pragma omp parallel for private(e, i) reduction(+:t) \
                schedule(static) num_threads(ntE)
            for (e = 0; e < E; e++){
                if (G->what_segment(Eu[e]) != G->what_segment(Ev[e])){
                    i = 2*e;
                    if (!G->arcs[i].is_active){
                        G->arcs[i].is_active = TRUE;
                        G->arcs[i+1].is_active = TRUE;
                        t++;
                    }
                }
            }
            w += t;
        }
        /* free steepest cut stuff */
        free(DfS);
        if (verbose){ printf("%d new activated edge(s).\n", w); FLUSH; }

        /***  check for no activation  ***/
        if (w == 0){ /**  recomputing everything is not worth  **/
            if (CP_difTol > ZERO || Dif != NULL){
                dif = ZERO;
                if (Dif != NULL){ Dif[CP_it_] = dif; } 
            }
            CP_it_++;
            if (Obj != NULL){ Obj[CP_it_] = Obj[CP_it_-1]; }
            continue;
        }else{ /* reduced values will be recomputed */
            FREE(*rX);
        }
        
        /***  compute reduced graph  ***/
        if (verbose){ printf("\tConstruct reduced problem... "); FLUSH; }
        /**  compute connected components  **/
        /* cleanup assigned components */
        for (v = 0; v < V; v++){ Cv[v] = -1; }
        rV_ = 0; /* current connected components */
        n = 0; /* number of vertices already assigned */
        i = 0; /* index of vertice currently exploring */
        free(rVc);
        rVc_ = (int*) malloc((V + 1)*sizeof(int));
        rVc_[0] = 0;
        /* depth first search */
        for (u = 0; u < V; u++){
            if (Cv[u] != -1){ continue; } /* already assigned */
            Cv[u] = rV_; /* assign to current component */
            Vc[n++] = u; /* put in connected components list */
            while (i < n){
                v = Vc[i++]; 
                /* add neighbors to the connected components list */
                for (ee = G->nodes[v].first; ee; ee = ee->next){
                    if (!ee->is_active){
                        w = (int) (ee->head - G->nodes);
                        if (Cv[w] != -1){ continue; }
                        Cv[w] = rV_;
                        Vc[n++] = w;
                    }
                }
            } /* the current connected component is complete */
            rVc_[++rV_] = n;
        }
        /* update cumulative components size and number of parallel threads */
        rVc = (int*) realloc(rVc_, (rV_ + 1)*sizeof(int)); /* this frees rVc_ */
        ntrV = compute_num_threads(rV_, rV_);
        ntV_rV = compute_num_threads(V, rV_);
        ntNV_rV = compute_num_threads(N*V, rV_);
        ntVV_rV = compute_num_threads(V*V, rV_);
        ntrVrV = compute_num_threads(rV_*rV_, rV_);
        ntNrV = compute_num_threads(N*rV_, N);
        ntrVN = compute_num_threads(N*rV_, rV_);

        /**  compute reduced connectivity and penalizations  **/
        /* rEc has two purposes
         * 1) keep track of the number of edges going out of a component
         * 2) maps neighboring components to reduced edges
         * note that both are simultaneously possible because we consider
         * undirected edges, indexed by (ru, rv) such that ru < rv */
        rEc = (int*) malloc(rV_*sizeof(int));
        rEv = (int*) malloc(E*sizeof(int));
        rLa_d1 = (real*) malloc(E*sizeof(real));
        for (rv = 0; rv < rV_; rv++){ rEc[rv] = -1; }
        rE = 0; /* current number of reduced edges */
        n = 0; /* keep track of previous edge number */
        /* iterate over components */
        for (ru = 0; ru < rV_; ru++){
            i = 1; /* flag signalling isolated components */
            /* run along the component ru */
            for (u = Vc[s = rVc[ru]], t = rVc[ru+1]; s < t; u = Vc[++s]){
                for (ee = G->nodes[u].first; ee; ee = ee->next){
                    if (!ee->is_active){ continue; }
                    e = ((int) (ee - G->arcs))/2;
                    a = La_d1[e];
                    if (a == ZERO){ continue; }
                    i = 0; /* a nonzero edge involving ru exists */
                    v = (int) (ee->head - G->nodes);
                    rv = Cv[v];
                    if (rv < ru){ continue; } /* count only undirected edges */
                    re = rEc[rv];
                    if (re == -1){ /* new edge */
                        rEv[rE] = rv;
                        rLa_d1[rE] = a;
                        rEc[rv] = rE++;
                    }else{ /* edge already exists */
                        rLa_d1[re] += a;
                    }
                }
            }
            if (i){ /* isolated components must be linked to themselves for PFDR */
                rEv[rE] = ru;
                rLa_d1[rE++] = eps;
            }else{
                for (; n < rE; n++){ rEc[rEv[n]] = -1; } /* reset rEc */
            }
            rEc[ru] = rE;
        }
        /* update reduced edge list and number of parallel threads */
        rEv = (int*) realloc(rEv, rE*sizeof(int));
        rEu = (int*) malloc(rE*sizeof(int));
        rLa_d1 = (real*) realloc(rLa_d1, rE*sizeof(real));
        re = 0;
        for (ru = 0; ru < rV_; ru++){ while(re < rEc[ru]){ rEu[re++] = ru; } }
        free(rEc);
        ntrE = compute_num_threads(rE, rE);

        /**  compute reduced matrix  **/
        /* rule of thumb to decide for premultiplication by A^t for PFDR:
         * without premultiplication: 2 N rV i operations
         * - two matrix-vector mult. per PFDR iter. : 2 N rV i
         * with premultiplication: N rV^2 + rV^2 i operations
         * - compute symmetrized reduced matrix : N rV^2
         * - one matrix-vector mult. per PFDR iter. : rV^2 i
         * premultiplication if rV < (2 N i)/(N + i) */
        preAt = (N <= 0) || (rV_ < (2*N*PFDR_it)/(N + PFDR_it));
        if (preAt){
            rY = (real*) malloc(rV_*sizeof(real));
            if (N == 0){ rAA = (real*) malloc(rV_*sizeof(real)); }
            else{ rAA = (real*) malloc(rV_*rV_*sizeof(real)); }
        }
        if (N > 0){ /* direct matricial case */
            rA = (real*) calloc(N*rV_, sizeof(real));
            #pragma omp parallel for private(rv, rAv, s, t, v, Av, n) \
                schedule(static) num_threads(ntNV_rV)
            for (rv = 0; rv < rV_; rv++){
                rAv = rA + N*rv;
                /* run along the component rv */
                for (v = Vc[s = rVc[rv]], t = rVc[rv+1]; s < t; v = Vc[++s]){
                    Av = A + N*v;
                    for (n = 0; n < N; n++){ rAv[n] += Av[n]; }
                }
            }
            if (preAt){
                /* upper triangular part */
                #pragma omp parallel for private(rv, ru, n, i, rAv, Av, a) \
                    schedule(static) num_threads(ntrVrV)
                for (ru = 0; ru < rV_; ru++){
                    i = 0;
                    Av = rA + N*ru;
                    rAv = rAA + rV_*ru;
                    for (rv = 0; rv <= ru; rv++){
                        a = ZERO;
                        for (n = 0; n < N; n++){ a += rA[i++]*Av[n]; }
                        rAv[rv] = a;
                    }
                }
                /* correlation with observation Y */
                #pragma omp parallel for private(rv, v, s, t, a) \
                    schedule(static) num_threads(ntrVN)
                for (rv = 0; rv < rV_; rv++){
                    a = ZERO;
                    rAv = rA + N*rv;
                    for (n = 0; n < N; n++){ a += rAv[n]*Y[n]; }
                    rY[rv] = a;
                }
            }
        }else{ /* premultiplied by A^t */
            /* observation Y is actually A^t Y */
            #pragma omp parallel for private(rv, v, s, t, a) schedule(static) \
                num_threads(ntV_rV)
            for (rv = 0; rv < rV_; rv++){
                a = ZERO;
                /* run along the component rv */
                for (v = Vc[s = rVc[rv]], t = rVc[rv+1]; s < t; v = Vc[++s]){
                    a += Y[v];
                }
                rY[rv] = a;
            }
            if (N < 0){ /* upper triangular part */
                #pragma omp parallel for \
                    private(ru, rv, rAv, Av, s, t, u, v, n, i, a) \
                    schedule(static) num_threads(ntVV_rV)
                for (ru = 0; ru < rV_; ru++){
                    rAv = rAA + rV_*ru;
                    for (rv = 0; rv <= ru; rv++){
                        a = ZERO;
                        /* run along the component ru */
                        for (u = Vc[s = rVc[ru]], t = rVc[ru+1]; s < t; u = Vc[++s]){
                            Av = A + V*u;
                            /* run along the component rv */
                            for (v = Vc[n = rVc[rv]], i = rVc[rv+1]; n < i; v = Vc[++n]){
                                a += Av[v];
                            }
                        }
                        rAv[rv] = a;
                    }
                }
            }else{ /* diagonal case */
                #pragma omp parallel for private(rv, s, t, v, a) \
                    schedule(static) num_threads(ntV_rV)
                for (rv = 0; rv < rV_; rv++){
                    if (A != NULL){
                        a = ZERO;
                        /* run along the component rv */
                        for (v = Vc[s = rVc[rv]], t = rVc[rv+1]; s < t; v = Vc[++s]){
                            a += A[v];
                        }
                        rAA[rv] = a;
                    }else{ /* identity */
                        rAA[rv] = rVc[rv+1] - rVc[rv];
                    }
                }
            }
        }
        if (preAt && N != 0){ /* fill lower triangular part */
            #pragma omp parallel for private(ru, rv, rAv, i) schedule(static) \
                num_threads(ntrVrV)
            for (ru = 0; ru < rV_ - 1; ru++){
                rAv = rAA + rV_*ru;
                i = rV_ + (rV_ + 1)*ru;
                for (rv = ru+1; rv < rV_; rv++){
                    rAv[rv] = rAA[i];
                    i += rV_;
                }
            }
        }

        /**  equilibration and Lipschitz metric  **/
        if (N == 0){
            L = rAA;
        }else{
            L = (real*) malloc(rV_*sizeof(real));
            if (preAt){
                /* Jacobi equilibration */
                #pragma omp parallel for private(rv) schedule(static) \
                    num_threads(ntrV)
                for (rv = 0; rv < rV_; rv++){ L[rv] = sqrt(rAA[rv*(rV_ + 1)]); }
                #pragma omp parallel for private(ru, rv, rAv, a) \
                    schedule(static) num_threads(ntrVrV)
                for (ru = 0; ru < rV_; ru++){
                    rAv = rAA + rV_*ru;
                    a = L[ru];
                    for (rv = 0; rv < rV_; rv++){ rAv[rv] /= (a*L[rv]); }
                }
                /* compute operator norm */
                c = operator_norm_matrix(0, rV_, rAA, normTol, normItMax, normNbInit, 0);
                /* revert equilibration */
                #pragma omp parallel for private(ru, rv, rAv, a) \
                    schedule(static) num_threads(ntrVrV)
                for (ru = 0; ru < rV_; ru++){
                    rAv = rAA + rV_*ru;
                    a = L[ru];
                    for (rv = 0; rv < rV_; rv++){ rAv[rv] *= (a*L[rv]); }
                }
            }else{
                /* Jacobi equilibration */
                #pragma omp parallel for private (rv, n, rAv, a, b) \
                    schedule(static) num_threads(ntrVN)
                for (rv = 0; rv < rV_; rv++){ 
                    rAv = rA + N*rv;
                    a = ZERO;
                    for (n = 0; n < N; n++){
                        b = rAv[n];
                        a += b*b;
                    }
                    L[rv] = sqrt(a);
                }
                #pragma omp parallel for private(rv, n, rAv, a) \
                    schedule(static) num_threads(ntrVN)
                for (rv = 0; rv < rV_; rv++){
                    rAv = rA + N*rv;
                    a = L[rv];
                    for (n = 0; n < N; n++){ rAv[n] /= a; }
                }
                /* compute operator norm */
                c = operator_norm_matrix(N, rV_, rA, normTol, normItMax, normNbInit, 0);
                /* revert equilibration */
                #pragma omp parallel for private(rv, n, rAv, a) \
                    schedule(static) num_threads(ntrVN)
                for (rv = 0; rv < rV_; rv++){
                    rAv = rA + N*rv;
                    a = L[rv];
                    for (n = 0; n < N; n++){ rAv[n] *= a; }
                }
            }
            /* compute resulting Lipschitz metric */
            #pragma omp parallel for private(rv) schedule(static) \
                num_threads(ntrV)
            for (rv = 0; rv < rV_; rv++){ L[rv] *= L[rv]*c; }
        }
        if (verbose){
            printf("%d connected component(s), %d reduced edge(s).\n", rV_, rE);
            FLUSH;
        }

        /***  preconditioned forward-Douglas-Rachford  ***/
        if (verbose){
            printf("\tSolve reduced problem:\n", rV_, rE);
            FLUSH;
        }
        *rX = (real*) CALLOC(rV_, sizeof(real));
        if (preAt){
            n = (N == 0) ? 0 : -rV_;
            PFDR_graph_quadratic_d1_bounds<real>(rV_, rE, n, \
                *rX, rY, rAA, rEu, rEv, rLa_d1, min, max, \
                DIAG, L, PFDR_rho, PFDR_condMin, PFDR_difRcd, PFDR_difTol, \
                PFDR_itMax, &PFDR_it, NULL, NULL, verbose);
        }else{
            PFDR_graph_quadratic_d1_bounds<real>(rV_, rE, N, \
                *rX, Y, rA, rEu, rEv, rLa_d1, min, max, \
                DIAG, L, PFDR_rho, PFDR_condMin, PFDR_difRcd, PFDR_difTol, \
                PFDR_itMax, &PFDR_it, NULL, NULL, verbose);
        }
        /* free PFDR stuff */
        if (N != 0){ free(L); }

        /***  merge neighboring components with almost equal values  ***/
        /* this only deactivates edges, components are not updated yet */
        #pragma omp parallel for private(e, i, a, b, d) schedule(static) \
            num_threads(ntE)
        for (e = 0; e < E; e++){
            i = 2*e;
            if (G->arcs[i].is_active){
                /* values and finite difference */
                a = (*rX)[Cv[Eu[e]]];
                b = (*rX)[Cv[Ev[e]]];
                d = a - b;
                /* absolute values */
                if (a < ZERO){ a = -a; }
                if (b < ZERO){ b = -b; }
                if (d < ZERO){ d = -d; }
                /* max and relative difference */
                if (a < b){ a = b; }
                d = (a > eps) ? d/a : d/eps;
                if (d <= CP_difTol){
                    G->arcs[i].is_active = FALSE;
                    G->arcs[i+1].is_active = FALSE;
                }
            }
        }

        /***  progress  ***/
        if (N > 0){ /** direct matricial case, compute residual R = Y - A X  **/
            R = rA; /* can be done in-place */
            #pragma omp parallel for private(n, a, i, rv) schedule(static) \
                num_threads(ntNrV)
            for (n = 0; n < N; n++){
                a = ZERO;
                i = n;
                for (rv = 0; rv < rV_; rv++){
                    a += rA[i]*(*rX)[rv];
                    i += N;
                }
                R[n] = Y[n] - a;
            }
            R = (real*) realloc(R, N*sizeof(real)); /* this frees rA */
        }
  
        /**  iterate evolution  **/
        if (CP_difTol > ZERO || Dif != NULL){
            dif = ZERO;
            c = ZERO;
            #pragma omp parallel for private(v, rv, a, b) reduction(+:dif, c) \
                schedule(static) num_threads(ntV)
            for (v = 0; v < V; v++){
                rv = Cv[v];
                a = (*rX)[rv];
                b = rX_[Cv_[v]] - a;
                dif += b*b;
                c += a*a;
                Cv_[v] = rv;
            }
            dif = (c > eps) ? dif/c : dif/eps;
            free(rX_);
            rX_ = (real*) malloc(rV_*sizeof(real));
            for (rv = 0; rv < rV_; rv++){ rX_[rv] = (*rX)[rv]; }
            if (Dif != NULL){ Dif[CP_it_] = dif; } 
        }

        CP_it_++;

        /**  objective functional value  **/
        if (Obj != NULL){ 
            a = ZERO;
            if (N > 0){ /* direct matricial case, 1/2 ||Y - A X||^2 */
                #pragma omp parallel for private(n) reduction(+:a) \
                    schedule(static) num_threads(ntN)
                for (n = 0; n < N; n++){ a += R[n]*R[n]; }
                a *= HALF;
            }else{ /* premultiplied by A^t, 1/2 <X, A^t A X> - <X, A^t Y> */
                if (N < 0){ /* full matrix */
                    #pragma omp parallel for private(ru, rv, b) \
                        reduction(+:a) schedule(static) num_threads(ntrVrV)
                    for (ru = 0; ru < rV_; ru++){
                            rAv = rAA + rV_*ru;
                            b = ZERO;
                            for (rv = 0; rv < rV_; rv++){ b += rAv[rv]*(*rX)[rv]; }
                            a += (*rX)[ru]*(HALF*b - rY[ru]); 
                    }
                }else{ /* diagonal matrix */
                    #pragma omp parallel for private(rv) reduction(+:a) \
                        schedule(static) num_threads(ntrV)
                    for (rv = 0; rv < rV_; rv++){
                        a += (*rX)[rv]*(HALF*rAA[rv]*(*rX)[rv] - rY[rv]);
                    }
                }
            }
            Obj[CP_it_] = a;
            /* ||x||_{d1,La_d1} */
            a = ZERO;
            #pragma omp parallel for private(re, b) reduction(+:a) \
                schedule(static) num_threads(ntrE)
            for (re = 0; re < rE; re++){
                b = (*rX)[rEu[re]] - (*rX)[rEv[re]];
                if (b < ZERO){ b = -b; }
                a += rLa_d1[re]*b;
            }
            Obj[CP_it_] += a;
        }

        /***  free reduced graph stuff  ***/
        free(rEu);
        free(rEv);
        free(rLa_d1);
        free(rAA); rAA = NULL;
        free(rY); rY = NULL;
        
    } /* endwhile (true) */

    /* final information */
    *rV = rV_;
    *CP_it = CP_it_;

    /* free CP stuff, or store for warm restart */
    if (CP_restart != NULL){
        CP_restart->G = G;
        CP_restart->Vc = Vc;
        CP_restart->rVc = rVc;
        CP_restart->R = R;
    }else{
        delete G;
        free(Vc);
        free(rVc);
        free(R); 
    }

    /* free remaining stuff */
    free(rX_);
    free(Cv_);
}

/* instantiate for compilation */
template void CP_PFDR_graph_quadratic_d1_bounds<float>(const int, const int, \
    const int, int*, int*, float**, const float*, const float*, const int*, \
    const int*, const float*, const float, const float, const float, \
    const int, int*, const float, const float, const float, const float, \
    const int, double*, float*, float*, const int, struct CPqb_Restart<float>*);

template void CP_PFDR_graph_quadratic_d1_bounds<double>(const int, const int, \
    const int, int*, int*, double**, const double*, const double*, const int*, \
    const int*, const double*, const double, const double, const double, \
    const int, int*, const double, const double, const double, const double, \
    const int, double*, double*, double*, const int, struct CPqb_Restart<double>*);
