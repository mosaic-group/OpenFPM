// ------------------------------------------------------------------------------------------
//    Copyright (C) 2021 ---- absingh
//
//    This file is part of the Surface DCPSE Paper.
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
// ------------------------------------------------------------------------------------------

#include "Vector/vector_dist_subset.hpp"
#include "Operators/Vector/vector_dist_operators.hpp"
#include "DCPSE/DCPSE_op/DCPSE_surface_op.hpp"
#include "DCPSE/DCPSE_op/DCPSE_Solver.hpp"
#include "OdeIntegrators/OdeIntegrators.hpp"

#include <util/PathsAndFiles.hpp>

std::string p_output;

constexpr int DF=0;
constexpr int F=1;
constexpr int ANADF=2;
constexpr int ERR=3;
constexpr int NORMAL=4;
constexpr int THETA=5;
constexpr int TEMP=6;

double dt,tf,ALPHA=0.1;

typedef aggregate<double,double,double,double,Point<2,double>,double,Point<2,double>> prop;
openfpm::vector<std::string> propNames{{"Df","f","AnaDf","error","Normal","Theta","TEMP"}};
typedef vector_dist_ws<2,double,prop> vector_type;


int main(int argc, char * argv[]) {
  openfpm_init(&argc,&argv);
  auto & v_cl = create_vcluster();
  timer tt;
  tt.start();
  size_t n,k;
  // Get command line arguments
  if (argc > 4) {
    std::cout << "Warning: The executable requires the following arguments: number_grid_points FrequencyMode Order" << std::endl;
    return 1;
  }
    n = std::stoi(argv[1]);
    k = std::stoi(argv[2]);
    unsigned int ord = std::stoi(argv[3]);

  // Files and directories
  std::string cwd{boost::filesystem::current_path().string()};
  std::string output_dir{cwd + "/output_circle_Poisson"};
  create_directory_if_not_exist(output_dir);
  p_output = output_dir + "/particles";

  // Domain
  double boxP1{-1.5}, boxP2{1.5};
  double boxSize{boxP2 - boxP1};
  size_t sz[2] = {n,n};
  double grid_spacing{boxSize/(sz[0]-1)};
  double rCut{3.9 * grid_spacing};

  Box<2,double> domain{{boxP1,boxP1},{boxP2,boxP2}};
  size_t bc[2] = {NON_PERIODIC,NON_PERIODIC};
  Ghost<2,double> ghost{rCut + grid_spacing/8.0};
  
  // Particles
  vector_type particles{0,domain,bc,ghost};
  particles.setPropNames(propNames);

  Point<2,double> coord;
  // 1. Particles on the Circular surface
  double theta{0.0};
  double dtheta{2*M_PI/double(n)};
  if (v_cl.rank() == 0) {
    for (int i = 0; i < n; ++i) {    
      coord[0] =   std::cos(theta);
      coord[1] =   std::sin(theta);
      
      particles.add();
      particles.getLastPos()[0] = coord[0];
      particles.getLastPos()[1] = coord[1];

      particles.getLastProp<F>() = std::sin(theta)+std::cos(theta);

      particles.getLastProp<NORMAL>()[0] = std::cos(theta);
      particles.getLastProp<NORMAL>()[1] = std::sin(theta);
      particles.getLastProp<ANADF>() = std::sin(k*theta);;
      particles.getLastProp<DF>() = -openfpm::math::intpowlog(k,2)*std::sin(k*theta);
      particles.getLastProp<THETA>() = theta;
      particles.getLastSubset(0);
      if(coord[0]==1. && coord[1]==0.)
      {particles.getLastSubset(1);}
      theta += dtheta;
    }
    std::cout << "n: " << n << " - grid spacing: " << grid_spacing << " - rCut: " << rCut << " - Dtheta: " << dtheta<<std::endl;
  }

  particles.map();
  particles.ghost_get<0>();
  std::cout << "size: " << particles.size_local() << std::endl;
  particles.deleteGhost();
  particles.write_frame(p_output,0,BINARY);

  vector_dist_subset<2,double,prop> particles_bulk(particles,0);
  vector_dist_subset<2, double,prop> particles_boundary(particles,1);
  auto & bulk=particles_bulk.getIds();
  auto & boundary=particles_boundary.getIds();

  auto verletList = particles.template getVerlet<VL_NON_SYMMETRIC|VL_SKIP_REF_PART>(rCut);
  //SurfaceDerivative_x<NORMAL> Sdx(particles, 2, rCut,grid_spacing);
  //SurfaceDerivative_y<NORMAL> Sdy(particles, 2, rCut,grid_spacing);
  SurfaceDerivative_xx<NORMAL, decltype(verletList)> Sdxx(particles, verletList, ord, rCut, grid_spacing, rCut/grid_spacing);
  SurfaceDerivative_yy<NORMAL, decltype(verletList)> Sdyy(particles, verletList, ord, rCut, grid_spacing, rCut/grid_spacing);
  //SurfaceDerivative_xy<NORMAL> Sdxy(particles, 2, rCut,grid_spacing);

  auto f=getV<F>(particles);
  auto df=getV<DF>(particles);
  auto Af=getV<ANADF>(particles);
  particles.ghost_get<F>();
  
  DCPSE_scheme<equations2d1,decltype(particles)> Solver(particles);
  Solver.impose(Sdxx(f)+Sdyy(f), bulk, df);
  Solver.impose(f, boundary, Af);
  Solver.solve(f);

  // Error
  double MaxError=0,L2=0;
  auto it=particles.getDomainIterator();
  while(it.isNext()){
      auto p = it.get();
      Point<2,double> xp=particles.getPos(p);
      double theta=particles.getProp<THETA>(p);
      particles.getProp<ERR>(p) = fabs(particles.getProp<F>(p)-particles.getProp<ANADF>(p));
          if (particles.getProp<ERR>(p)>MaxError){
              MaxError=particles.getProp<ERR>(p);
          }
          L2+=particles.getProp<ERR>(p)*particles.getProp<ERR>(p);
          ++it;
  }
  v_cl.sum(L2);
  v_cl.max(MaxError);
  v_cl.execute();
  L2=sqrt(L2);
  std::cout.precision(30);
  if (v_cl.rank()==0){
      std::cout<<"dTheta:"<<dtheta<<std::endl;
      std::cout<<"L2:"<<L2<<std::endl;
      std::cout<<"L_inf:"<<MaxError<<std::endl;
  }
  particles.deleteGhost();
  particles.write(p_output,BINARY);
  Sdxx.deallocate(particles);
  Sdyy.deallocate(particles);

  tt.stop();
  if (v_cl.rank() == 0)
    std::cout << "Simulation took: " << tt.getcputime() << " s (CPU) - " << tt.getwct() << " s (wall)\n";
  
  openfpm_finalize();
  return 0;

}