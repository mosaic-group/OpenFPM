//
// Created by Abhinav Singh on 15.06.20.
//

#include "config.h"
#include <iostream>
#include "DCPSE/DCPSE_OP/DCPSE_op.hpp"
#include "DCPSE/DCPSE_OP/DCPSE_Solver.hpp"
#include "Operators/Vector/vector_dist_operators.hpp"
#include "Vector/vector_dist_subset.hpp"

int main(int argc, char* argv[])
{

    {   openfpm_init(&argc,&argv);
        timer tt2;
        tt2.start();
        size_t gd_sz = 81;
        double Re=100;
        double V_err_eps = 1e-2;
        double alpha=0.0125;

        constexpr int x = 0;
        constexpr int y = 1;
        const size_t sz[2] = {gd_sz,gd_sz};
        Box<2, double> box({0, 0}, {1,1});
        size_t bc[2] = {NON_PERIODIC, NON_PERIODIC};
        double spacing;
        spacing = 1.0 / (sz[0] - 1);
        double rCut = 3.1 * spacing;
        int ord = 2;
        double sampling_factor = 3.1;
        double sampling_factor2 = 1.9;
        
        Ghost<2, double> ghost(rCut);
        auto &v_cl = create_vcluster();
        typedef aggregate<double, VectorS<2, double>, VectorS<2, double>,VectorS<2, double>,double,VectorS<2, double>,double,double> LidCavity;

        vector_dist_ws<2, double, LidCavity> Particles(0, box,bc,ghost);
        Particles.setPropNames({"00-Pressure","01-Velocity","02-RHS","03-dV","04-Divergence","05-Divergence","06-H","07-dP"});

        double x0, y0, x1, y1;
        x0 = box.getLow(0);
        y0 = box.getLow(1);
        x1 = box.getHigh(0);
        y1 = box.getHigh(1);

        auto it = Particles.getGridIterator(sz);
        while (it.isNext())
        {
            Particles.add();
            auto key = it.get();
            mem_id k0 = key.get(0);
            double xp = k0 * spacing;
            Particles.getLastPos()[0] = xp;
            mem_id k1 = key.get(1);
            double yp = k1 * spacing;
            Particles.getLastPos()[1] = yp;
            Particles.getLastProp<1>()[0]=0.0;
            Particles.getLastProp<1>()[1]=0.0;
            if (xp != x0 && yp != y0 && xp != x1 && yp != y1)
            {
                Particles.getLastSubset(0);
            }
            else if(yp==y1 && xp>x0 && xp<x1)
            {
                Particles.getLastSubset(1);
                Particles.getLastProp<1>()[0]=1.0;
            }
            else if(xp==0)
            {
                Particles.getLastSubset(3);
            }
            else if(xp==x1)
            {
                Particles.getLastSubset(4);
            }
            else
            {
                Particles.getLastSubset(2);
            }
            ++it;
        }

        Particles.map();
        Particles.ghost_get<0>();


        vector_dist_subset<2, double, LidCavity> Particles_bulk(Particles,0);
        vector_dist_subset<2, double, LidCavity> Particles_up(Particles,1);
        vector_dist_subset<2, double, LidCavity> Particles_down(Particles,2);
        vector_dist_subset<2, double, LidCavity> Particles_left(Particles,3);
        vector_dist_subset<2, double, LidCavity> Particles_right(Particles,4);
        auto &bulk=Particles_bulk.getIds();
        auto &up_p=Particles_up.getIds();
        auto &dw_p=Particles_down.getIds();
        auto &l_p=Particles_left.getIds();
        auto &r_p=Particles_right.getIds();


        auto P = getV<0>(Particles);
        auto V = getV<1>(Particles);
        auto RHS = getV<2>(Particles);
        auto dV = getV<3>(Particles);
        auto div = getV<4>(Particles);
        auto V_star = getV<5>(Particles);
        auto H = getV<6>(Particles);
        auto dP = getV<7>(Particles);



        auto P_bulk = getV<0>(Particles_bulk);
        auto V_bulk = getV<1>(Particles_bulk);
        auto RHS_bulk =getV<2>(Particles_bulk);
        auto V_star_bulk = getV<5>(Particles_bulk);
        auto dP_bulk = getV<7>(Particles_bulk);


        P_bulk = 0;
        Derivative_x Dx(Particles, 2, rCut,sampling_factor, support_options::RADIUS);
        Derivative_xx Dxx(Particles, 2, rCut,sampling_factor2, support_options::RADIUS);
        Derivative_yy Dyy(Particles, 2, rCut,sampling_factor2, support_options::RADIUS);
        Derivative_y Dy(Particles, 2, rCut,sampling_factor, support_options::RADIUS);
        Derivative_x Bulk_Dx(Particles_bulk, 2, rCut,sampling_factor, support_options::RADIUS);
        Derivative_y Bulk_Dy(Particles_bulk, 2, rCut,sampling_factor, support_options::RADIUS);

        int n = 0, nmax = 300, ctr = 0, errctr=1, Vreset = 0;
        double V_err=1;
        if (Vreset == 1) {
            P_bulk = 0;
            P = 0;
            Vreset = 0;
        }
        P=0;

        eq_id vx,vy;
        vx.setId(0);
        vy.setId(1);
/*          W.setVarId(0);
        Sf.setVarId(1);
*/     

        double sum, sum1, sum_k,V_err_old;
        auto StokesX=(V[x]*Dx(V_star[x])+V[y]*Dy(V_star[x]))-(1.0/Re)*(Dxx(V_star[x])+Dyy(V_star[x]));
        auto StokesY=(V[x]*Dx(V_star[y])+V[y]*Dy(V_star[y]))-(1.0/Re)*(Dxx(V_star[y])+Dyy(V_star[y]));
        petsc_solver<double> solverPetsc;
        solverPetsc.setSolver(KSPGMRES);
        RHS[x] = 0.0;
        RHS[y] = 0.0;
        dV=0;
        P=0;
        timer tt;
        while (V_err >= V_err_eps && n <= nmax) {
            if (n%5==0){
                Particles.ghost_get<0,1,2,3,4,5,6,7>(SKIP_LABELLING);
                Particles.deleteGhost();
                Particles.write_frame("LID",n,BINARY);
                Particles.ghost_get<0>();
                }
            tt.start();
            Particles.ghost_get<0>(SKIP_LABELLING);
            RHS_bulk[x] = -Bulk_Dx(P);
            RHS_bulk[y] = -Bulk_Dy(P);
            DCPSE_scheme<equations2d2, decltype(Particles)> Solver(Particles);
            Solver.impose(StokesX, bulk, RHS[x], vx);
            Solver.impose(StokesY, bulk, RHS[y], vy);
            Solver.impose(V_star[x], up_p, 1.0, vx);
            Solver.impose(V_star[y], up_p, 0.0, vy);
            Solver.impose(V_star[x], l_p, 0.0, vx);
            Solver.impose(V_star[y], l_p, 0.0, vy);
            Solver.impose(V_star[x], r_p, 0.0, vx);
            Solver.impose(V_star[y], r_p, 0.0, vy);
            Solver.impose(V_star[x], dw_p, 0.0, vx);
            Solver.impose(V_star[y], dw_p, 0.0, vy);
            Solver.solve_with_solver(solverPetsc,V_star[x], V_star[y]);
    
            Particles.ghost_get<5>(SKIP_LABELLING);
            div = (Dx(V_star[x]) + Dy(V_star[y]));

           DCPSE_scheme<equations2d1E,decltype(Particles)> SolverH(Particles,options_solver::LAGRANGE_MULTIPLIER);
            auto Helmholtz = Dxx(H)+Dyy(H);
            SolverH.impose(Helmholtz,bulk,prop_id<4>());
            SolverH.impose(Dy(H), up_p,0);
            SolverH.impose(Dx(H), l_p,0);
            SolverH.impose(Dx(H), r_p,0);
            SolverH.impose(Dy(H), dw_p,0);
            //SolverH.solve_with_solver(solverPetsc2,H);
            SolverH.solve(H);
            Particles.ghost_get<6>(SKIP_LABELLING);
            Particles.ghost_get<6>(SKIP_LABELLING);
            P_bulk = P - alpha*(div-0.5*(V[x]*Bulk_Dx(H)+V[y]*Bulk_Dy(H)));
            V_star_bulk[0] = V_star[0] - Bulk_Dx(H);
            V_star_bulk[1] = V_star[1] - Bulk_Dy(H);
            sum = 0;
            sum1 = 0;

            for (int j = 0; j < bulk.size(); j++) {
                auto p = bulk.get<0>(j);
                sum += (Particles.getProp<5>(p)[0] - Particles.getProp<1>(p)[0]) *
                       (Particles.getProp<5>(p)[0] - Particles.getProp<1>(p)[0]) +
                       (Particles.getProp<5>(p)[1] - Particles.getProp<1>(p)[1]) *
                       (Particles.getProp<5>(p)[1] - Particles.getProp<1>(p)[1]);
                sum1 += Particles.getProp<5>(p)[0] * Particles.getProp<5>(p)[0] +
                        Particles.getProp<5>(p)[1] * Particles.getProp<5>(p)[1];
            }

            V = V_star;
            v_cl.sum(sum);
            v_cl.sum(sum1);
            v_cl.execute();
            sum = sqrt(sum);
            sum1 = sqrt(sum1);
            V_err_old = V_err;
            V_err = sum / sum1;
            if (V_err > V_err_old || abs(V_err_old - V_err) < 1e-8) {
                errctr++;
                //alpha_P -= 0.1;
            } else {
                errctr = 0;
            }
            if (n > 3) {
                if (errctr > 5) {
                    Vreset = 1;
                    errctr=0;
                    //break;
                } else {
                    Vreset = 0;
                }
            }
            n++;
            tt.stop();
            if (v_cl.rank() == 0) {
                std::cout << "Rel l2 cgs err in V = " << V_err << " at " << n << " and took " <<tt.getwct() <<"("<<tt.getcputime()<<") seconds(CPU)." << std::endl;
            }
        }
        Particles.deleteGhost();
        Particles.write("LID");
        tt2.stop();
        if (v_cl.rank() == 0) {
            std::cout << "The simulation took " << tt2.getcputime() << "(CPU) ------ " << tt2.getwct()
                      << "(Wall) Seconds.";
        }
    }
    openfpm_finalize();

}