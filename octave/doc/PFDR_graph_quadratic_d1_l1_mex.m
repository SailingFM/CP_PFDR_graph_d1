function [X, it, Obj, Dif] = PFDR_graph_quadratic_d1_l1_mex(Y, A, Eu, Ev, La_d1, La_l1, positivity, L, rho, condMin, difRcd, difTol, itMax, verbose)
%
%        [X, it, Obj, Dif] = PFDR_graph_quadratic_d1_l1_mex(Y, A, Eu, Ev, La_d1, La_l1, positivity, L, rho, condMin, difRcd, difTol, itMax, verbose)
%
% minimize functional over a graph G = (V, E)
%
%       F(x) = 1/2 ||y - A x||^2 + ||x||_{d1,La_d1} + ||x||_{l1,La_l1}
%
% where y in R^N, x in R^V, A in R^{N-by-|V|}
%       ||x||_{d1,La_d1} = sum_{uv in E} la_d1_uv |x_u - x_v|,
%       ||x||_{l1,La_l1} = sum_{v  in V} la_l1_v |x_v|,
%
% with the possibility of adding a positivity constraint on the coordinates of x,
%       F(x) + i_{x >= 0}
%
% using preconditioned forward-Douglas-Rachford splitting algorithm.
%
% INPUTS: (warning: real numeric type is either single or double, not both)
% Y          - observations, array of length N (real)
% A          - matrix, N-by-V array (real)
% Eu         - for each edge, C-style index of one vertex, array of length E (int)
% Ev         - for each edge, C-style index of the other vertex, array of length E (int)
%              Every vertex should belong to at least one edge with a nonzero
%              penalization coefficient. If it is not the case, a workaround
%              is to add an edge from the vertex to itself with a very small
%              penalization coefficient.
% La_d1      - d1 penalization coefficients, array of length E (real)
% La_l1      - l1 penalization coefficients, array of length V (real)
%              give only one scalar (0 is fine) for no l1 penalization
% positivity - if nonzero, the positivity constraint is added
% L          - information on Lipschitzianity of the operator A^* A.
%              either a scalar satisfying 0 < L <= ||A^* A||,
%              or a diagonal matrix (array of length V (real)) satisfying
%               0 < L and ||L^(-1/2) A^* A L^(-1/2)|| <= 1
% rho        - relaxation parameter, 0 < rho < 2
%              1 is a conservative value; 1.5 often speeds up convergence
% condMin    - positive parameter ensuring stability of preconditioning
%              0 < condMin =< 1; 1e-3 is a conservative value; 1e-6 might 
%              enhance preconditioning
% difRcd     - reconditioning criterion on iterate evolution. A reconditioning
%              is performed if relative changes of X (in Euclidean norm) is less
%              than difRcd. It is then divided by 10.
%              10*difTol is a conservative value, 1e2*difTol or 1e3*difTol might
%              speed up convergence. reconditioning might temporarily draw 
%              minimizer away from solution; it is advised to monitor objective
%              value when using reconditioning
% difTol     - stopping criterion on iterate evolution. Algorithm stops if
%              relative changes of X (in Euclidean norm) is less than difTol.
%              1e-5 is a typical value; 1e-6 or less can give better
%              precision but with longer computational time.
% itMax      - maximum number of iterations
% verbose    - if nonzero, display information on the progress, every 'verbose'
%              iterations
% OUTPUTS:
% X   - final minimizer, array of length V (real)
% it  - actual number of iterations performed
% Obj - if requested, the values of the objective functional 
%       along iterations (it+1 values)
% Dif - if requested, the iterate evolution along iterations (see difTol)
% 
% Parallel implementation with OpenMP API.
%
% Typical compilation command (UNIX):
% mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp" LDFLAGS="\$LDFLAGS -fopenmp" ...
%     api/PFDR_graph_quadratic_d1_l1_mex.cpp ...
%     src/PFDR_graph_quadratic_d1_l1.cpp ...
%     -output bin/PFDR_graph_quadratic_d1_l1_mex
%
% Reference: H. Raguet, A Note on the Forward-Douglas-Rachford Splitting for
% Monotone Inclusion and Convex Optimization.
%
% Hugo Raguet 2016
