cd('mex/')

mkdir('bin/')

mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp" LDFLAGS="\$LDFLAGS -fopenmp" ...
    api/PFDR_graph_quadratic_d1_l1_mex.cpp ...
    src/PFDR_graph_quadratic_d1_l1.cpp ...
    -output bin/PFDR_graph_quadratic_d1_l1_mex

mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp" LDFLAGS="\$LDFLAGS -fopenmp" ...
    api/PFDR_graph_quadratic_d1_l1_AtA_mex.cpp ...
    src/PFDR_graph_quadratic_d1_l1.cpp ...
    -output bin/PFDR_graph_quadratic_d1_l1_AtA_mex

mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp" LDFLAGS="\$LDFLAGS -fopenmp" ...
    api/PFDR_graph_l22_d1_l1_mex.cpp src/PFDR_graph_quadratic_d1_l1.cpp ...
    -output bin/PFDR_graph_l22_d1_l1_mex


mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp" LDFLAGS="\$LDFLAGS -fopenmp" ...
    api/PFDR_graph_loss_d1_simplex_mex.cpp ...
    src/PFDR_graph_loss_d1_simplex.cpp src/proj_simplex_metric.cpp ...
    -output bin/PFDR_graph_loss_d1_simplex_mex


mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp" LDFLAGS="\$LDFLAGS -fopenmp" ...
    api/PFDR_graph_quadratic_d1_bounds_mex.cpp ...
    src/PFDR_graph_quadratic_d1_bounds.cpp ...
    -output bin/PFDR_graph_quadratic_d1_bounds_mex

mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp" LDFLAGS="\$LDFLAGS -fopenmp" ...
    api/PFDR_graph_quadratic_d1_bounds_AtA_mex.cpp ...
    src/PFDR_graph_quadratic_d1_bounds.cpp ...
    -output bin/PFDR_graph_quadratic_d1_bounds_AtA_mex

mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp" LDFLAGS="\$LDFLAGS -fopenmp" ...
    api/PFDR_graph_l22_d1_bounds_mex.cpp ...
    src/PFDR_graph_quadratic_d1_bounds.cpp ...
    -output bin/PFDR_graph_l22_d1_bounds_mex


mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp -DNDEBUG" ...
    LDFLAGS="\$LDFLAGS -fopenmp" ...
    api/CP_PFDR_graph_quadratic_d1_l1_mex.cpp ...
    src/CP_PFDR_graph_quadratic_d1_l1.cpp ...
    src/PFDR_graph_quadratic_d1_l1.cpp src/graph.cpp src/maxflow.cpp ...
    src/operator_norm_matrix.cpp -output bin/CP_PFDR_graph_quadratic_d1_l1_mex

mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp -DNDEBUG" ...
    LDFLAGS="\$LDFLAGS -fopenmp" ...
    api/CP_PFDR_graph_quadratic_d1_l1_AtA_mex.cpp ...
    src/CP_PFDR_graph_quadratic_d1_l1.cpp ...
    src/PFDR_graph_quadratic_d1_l1.cpp src/graph.cpp ...
    src/maxflow.cpp src/operator_norm_matrix.cpp ...
    -output bin/CP_PFDR_graph_quadratic_d1_l1_AtA_mex

mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp -DNDEBUG" ...
    LDFLAGS="\$LDFLAGS -fopenmp" ...
    api/CP_PFDR_graph_l22_d1_l1_mex.cpp ...
    src/CP_PFDR_graph_quadratic_d1_l1.cpp ...
    src/PFDR_graph_quadratic_d1_l1.cpp ...
    src/graph.cpp src/maxflow.cpp src/operator_norm_matrix.cpp ...
    -output bin/CP_PFDR_graph_l22_d1_l1_mex


mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp -DNDEBUG" ...
    LDFLAGS="\$LDFLAGS -fopenmp" ...
    api/CP_PFDR_graph_quadratic_d1_bounds_mex.cpp ...
    src/CP_PFDR_graph_quadratic_d1_bounds.cpp ...
    src/PFDR_graph_quadratic_d1_bounds.cpp ...
    src/graph.cpp src/maxflow.cpp src/operator_norm_matrix.cpp ...
    -output bin/CP_PFDR_graph_quadratic_d1_bounds_mex

mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp -DNDEBUG" ...
    LDFLAGS="\$LDFLAGS -fopenmp" ...
    api/CP_PFDR_graph_quadratic_d1_bounds_AtA_mex.cpp ...
    src/CP_PFDR_graph_quadratic_d1_bounds.cpp ...
    src/PFDR_graph_quadratic_d1_bounds.cpp src/graph.cpp ...
    src/maxflow.cpp src/operator_norm_matrix.cpp ...
    -output bin/CP_PFDR_graph_quadratic_d1_bounds_AtA_mex

mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp -DNDEBUG" ...
    LDFLAGS="\$LDFLAGS -fopenmp" ...
    api/CP_PFDR_graph_l22_d1_bounds_mex.cpp ...
    src/CP_PFDR_graph_quadratic_d1_bounds.cpp ...
    src/PFDR_graph_quadratic_d1_bounds.cpp ...
    src/graph.cpp src/maxflow.cpp src/operator_norm_matrix.cpp ...
    -output bin/CP_PFDR_graph_l22_d1_bounds_mex


mex CXXFLAGS="\$CXXFLAGS -DMEX -fopenmp -DNDEBUG" ...
    LDFLAGS="\$LDFLAGS -fopenmp" ...
    api/CP_PFDR_graph_loss_d1_simplex_mex.cpp ...
    src/CP_PFDR_graph_loss_d1_simplex.cpp ...
    src/PFDR_graph_loss_d1_simplex.cpp ...
    src/graph.cpp src/maxflow.cpp src/proj_simplex_metric.cpp ...
    -output bin/CP_PFDR_graph_loss_d1_simplex_mex