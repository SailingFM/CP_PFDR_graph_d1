function [Cv, rX, CP_it, Time, Obj, Dif] = CP_PFDR_graph_l22_d1_l1_mex(Y, La_l2, Eu, Ev, La_d1, La_l1, positivity, CP_difTol, CP_itMax, PFDR_rho, PFDR_condMin, PFDR_difRcd, PFDR_difTol, PFDR_itMax, verbose)
%
%        [Cv, rX, CP_it, Time, Obj, Dif] = CP_PFDR_graph_l22_d1_l1_mex(Y, La_l2, Eu, Ev, La_d1, La_l1, positivity, CP_difTol, CP_itMax, PFDR_rho, PFDR_condMin, PFDR_difRcd, PFDR_difTol, PFDR_itMax, verbose)
%
% minimize functional over a graph G = (V, E)
%
%       F(x) = 1/2 ||y - x||_{l2,La_l2}^2 + ||x||_{d1,La_d1}  + ||x||_{l1,La_l1}
%
% where x, y in R^V,
%      ||y - x||_{l2,La_l2}^2 = sum_{v in V} la_l2_v (y_v - x_v)^2,
%      ||x||_{d1,La_d1} = sum_{uv in E} la_d1_uv |x_u - x_v|,
%      ||x||_{l1,La_l1} = sum_{v  in V} La_l1_v |x_v|,
%
% with the possibility of adding a positivity constraint on the coordinates of x,
%       F(x) + i_{x >= 0}
%
% using cut-pursuit approach with preconditioned forward-Douglas-Rachford 
% splitting algorithm.
%
% INPUTS: (warning: real numeric type is either single or double, not both)
% Y          - observations, array of length V (real)
% La_l2      - l2 penalization coefficients, array of length V (real)
%              give only one scalar (1 is fine) for no weights on the l2-norm
% Eu         - for each edge, C-style index of one vertex, array of length E (int)
% Ev         - for each edge, C-style index of the other vertex, array of length E (int)
% La_d1      - d1 penalization coefficients, array of length E (real)
% La_l1      - l1 penalization coefficients, array of length V (real)
%              give only one scalar (0 is fine) for no l1 penalization
% positivity - if nonzero, the positivity constraint is added
%
% [CP]
% difTol     - stopping criterion on iterate evolution. Algorithm stops if
%              relative changes of X (in Euclidean norm) is less than difTol.
%              1e-5 is a typical value; 1e-6 or less can give better
%              precision but with longer computational time.
% itMax      - maximum number of iterations (graph cut and subproblem)
%              10 cuts solve accurately most problems
%
% [PFDR]
% rho        - relaxation parameter, 0 < rho < 2
%              1 is a conservative value; 1.5 often speeds up convergence
% condMin    - small positive parameter ensuring stability of preconditioning
%              0 < condMin =< 1; 1e-3 is a conservative value; 1e-6 might 
%              enhance preconditioning
% difRcd     - reconditioning criterion on iterate evolution. A reconditioning
%              is performed if relative changes of X (in Euclidean norm) is less
%              than difRcd. It is then divided by 10.
%              0 (no reconditioning) is a conservative value, 10*difTol or 
%              1e2*difTol might speed up convergence. reconditioning might 
%              temporarily draw minimizer away from solution, and give bad
%              subproblem solution
% difTol     - stopping criterion on iterate evolution. Algorithm stops if
%              relative changes of X (in Euclidean norm) is less than difTol.
%              1e-3*CP_difTol is a conservative value.
% itMax      - maximum number of iterations
%              1e4 iterations provides enough precision for most subproblems
%
% verbose    - if nonzero, display information on the progress, every 'verbose'
%              iterations during subproblem resolution
% OUTPUTS:
% Cv    - assignement of each vertex of the minimizer to an homogeneous connected
%         component of the graph, numbered from 0 to (rV - 1)
%         array of length V (int)
% rX    - values of each homogeneous connected components of the minimizer, 
%         array of length rV (real)
%         The actual minimizer is then reconstructed as X = rX(Cv+1);
% CP_it - actual number of iterations performed
% Time  - if requested, the elapsed time along iterations (itMax + 1 values)
% Obj   - if requested, the values of the objective functional along 
%         iterations (itMax + 1 values)
% Dif   - if requested, the iterate evolution along iterations (see difTol)
% 
% Parallel implementation with OpenMP API.
%
% Typical compilation command (UNIX):
% mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp -DNDEBUG" ...
%     LDFLAGS="\$LDFLAGS -fopenmp" ...
%     api/CP_PFDR_graph_l22_d1_l1_mex.cpp ...
%     src/CP_PFDR_graph_quadratic_d1_l1.cpp ...
%     src/PFDR_graph_quadratic_d1_l1.cpp ...
%     src/graph.cpp src/maxflow.cpp src/operator_norm_matrix.cpp ...
%     -output bin/CP_PFDR_graph_l22_d1_l1_mex
%
% Reference: H. Raguet and L. Landrieu, Cut-Pursuit Algorithm for Regularizing
% Nonsmooth Functionals with Graph Total Variation.
%
% Hugo Raguet 2017
