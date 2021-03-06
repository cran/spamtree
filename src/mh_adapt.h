#include <RcppArmadillo.h>

/*
const double EPS = 0.05; // 0.01
const double tau_accept = 0.234; // target
const double g_exp = .01;
const int g0 = 500; // iterations before starting adaptation
const double rho_max = 10;
const double rho_min = 10;
*/

const double EPS = 0.1; // 0.01
const double tau_accept = 0.234; // target
const double g_exp = .01;
const int g0 = 500; // iterations before starting adaptation
const double rho_max = 2;
const double rho_min = 5;


inline bool do_I_accept(double logaccept){ //, string name_accept, string name_count, List mcmc_pars){
  double acceptj = 1.0;
  if(!arma::is_finite(logaccept)){
    acceptj = 0.0;
  } else {
    if(logaccept < 0){
      acceptj = exp(logaccept);
    }
  }
  Rcpp::RNGScope scope;
  double u = R::runif(0,1);//arma::randu();
  if(u < acceptj){
    return true;
  } else {
    return false;
  }
}



class RAMAdapt {
public:
  
  // Robust adaptive MCMC Vihala 2012
  int p;
  arma::mat Ip;
  arma::mat paramsd;
  arma::mat Sigma; // (Ip + update)
  arma::mat S;
  double alpha_star;
  double eta;
  double gamma;
  
  // startup period variables
  int g0;
  int i;
  int c;
  bool flag_accepted;
  arma::mat prodparam;
  bool started;
  
  double propos_count;
  double accept_count;
  double accept_ratio;
  int history_length;
  arma::vec acceptreject_history;
  
  void count_proposal();
  void count_accepted();
  void update_ratios();
  void adapt(const arma::vec&, double, int);
  void print(int itertime, int mc);
  void print_summary(int time_tick, int time_mcmc, int m, int mcmc);
  
  RAMAdapt(){};
  RAMAdapt(int npars, const arma::mat& metropolis_sd);
};

inline RAMAdapt::RAMAdapt(int npars, const arma::mat& metropolis_sd){
  p = npars;
  alpha_star = .234;
  gamma = 0.5 + 1e-6;
  Ip = arma::eye(p,p);
  g0 = 50;
  S = metropolis_sd;
  
  paramsd = arma::chol(S, "lower");
  prodparam = paramsd / (g0 + 1.0);
  started = false;
  
  propos_count = 0;
  accept_count = 0;
  accept_ratio = 0;
  history_length = 200;
  acceptreject_history = arma::zeros(history_length);
  c = 0;
}

inline void RAMAdapt::count_proposal(){
  propos_count++;
  c ++;
  flag_accepted = false;
}

inline void RAMAdapt::count_accepted(){
  accept_count++;
  acceptreject_history(c % history_length) = 1;
  flag_accepted = true;
}

inline void RAMAdapt::update_ratios(){
  accept_ratio = accept_count/propos_count;
  if(!flag_accepted){
    acceptreject_history(c % history_length) = 0;
  }
}

inline void RAMAdapt::adapt(const arma::vec& U, double alpha, int mc){
  if(mc < g0){
    prodparam += U * U.t() / (mc + 1.0);
  } else {
    if(!started){
      paramsd = prodparam;
      started = true;
    }
    i = mc-g0;
    eta = std::min(1.0, (p+.0) * pow(i+1.0, -gamma));
    alpha = std::min(1.0, alpha);
    
    Sigma = Ip + eta * (alpha - alpha_star) * U * U.t() / arma::accu(U % U);
    //Rcpp::Rcout << "Sigma: " << endl << Sigma;
    S = paramsd * Sigma * paramsd.t();
    //Rcpp::Rcout << "S: " << endl << S;
    paramsd = arma::chol(S, "lower");
  }
}

inline void RAMAdapt::print(int itertime, int mc){
  Rprintf("%5d-th iteration [ %dms ] ~ MCMC acceptance %.2f%% (total: %.2f%%)\n", 
         mc+1, itertime, arma::mean(acceptreject_history)*100, accept_ratio*100);
}

inline void RAMAdapt::print_summary(int time_tick, int time_mcmc, int m, int mcmc){
  Rprintf("%.1f%% %dms (total: %dms) ~ MCMC acceptance %.2f%% (total: %.2f%%) \n",
         floor(100.0*(m+0.0)/mcmc),
         time_tick,
         time_mcmc,
         arma::mean(acceptreject_history)*100, accept_ratio*100);
}

inline double logistic(double x, double l=0, double u=1){
  return l + (u-l)/(1.0+exp(-x));
}

inline double logit(double x, double l=0, double u=1){
  return -log( (u-l)/(x-l) -1.0 );
}

inline arma::vec par_transf_fwd(arma::vec par){
  if(par.n_elem > 1){
    // gneiting nonsep 
    par(0) = log(par(0));
    par(1) = log(par(1));
    par(2) = logit(par(2));
    return par;
  } else {
    return log(par);
  }
}

inline arma::vec par_transf_back(arma::vec par){
  if(par.n_elem > 1){
    // gneiting nonsep 
    par(0) = exp(par(0));
    par(1) = exp(par(1));
    par(2) = logistic(par(2));
    return par;
  } else {
    return exp(par);
  }
}

//[[Rcpp::export]]
arma::vec par_huvtransf_fwd(arma::vec par, const arma::mat& set_unif_bounds);

//[[Rcpp::export]]
arma::vec par_huvtransf_back(arma::vec par, const arma::mat& set_unif_bounds);

inline bool unif_bounds(arma::vec& par, const arma::mat& bounds){
  bool out_of_bounds = false;
  for(unsigned int i=0; i<par.n_elem; i++){
    arma::rowvec ibounds = bounds.row(i);
    if( par(i) < ibounds(0) ){
      out_of_bounds = true;
      par(i) = ibounds(0) + 1e-10;
    }
    if( par(i) > ibounds(1) ){
      out_of_bounds = true;
      par(i) = ibounds(1) - 1e-10;
    }
  }
  return out_of_bounds;
}

inline double lognormal_proposal_logscale(const double& xnew, const double& xold){
  // returns  + log x' - log x
  // to be + to log prior ratio log pi(x') - log pi(x)
  return log(xnew) - log(xold);
}

inline double normal_proposal_logitscale(const double& x, double l=0, double u=1){
  return //log(xnew * (l-xnew)) - log(xold * (l-xold)); 
  -log(u-x) - log(x-l);
}

inline double lognormal_logdens(const double& x, const double& m, const double& ssq){
  return -.5*(2*M_PI*ssq) - .5/ssq * pow(log(x) - m, 2) - log(x);
}

inline double gamma_logdens(const double& x, const double& a, const double& b){
  return -lgamma(a) + a*log(b) + (a-1.0)*log(x) - b*x;
}
inline double invgamma_logdens(const double& x, const double& a, const double& b){
  return -lgamma(a) + a*log(b) + (-a-1.0)*log(x) - b/x;
}
inline double beta_logdens(const double& x, const double& a, const double& b, const double& c=1.0){
  // unnormalized
  return (a-1.0)*log(x) + (b-1.0)*log(c-x);
}

inline double calc_jacobian(const arma::vec& new_param, const arma::vec& param, 
                            const arma::mat& set_unif_bounds){
  
  double jac = 0;
  for(unsigned int j=0; j<param.n_elem; j++){
    jac += normal_proposal_logitscale(param(j), set_unif_bounds(j, 0), set_unif_bounds(j, 1)) -
      normal_proposal_logitscale(new_param(j), set_unif_bounds(j, 0), set_unif_bounds(j, 1));
  }
  return jac;
}


inline double calc_prior_logratio(const arma::vec& new_param, 
                                  const arma::vec& param, double a=2, double b=1){
  
  double plr=0;
  for(unsigned int j=0; j<param.n_elem; j++){
    plr += 
      invgamma_logdens(new_param(0), a, b) -
      invgamma_logdens(param(0), a, b);
  }
  return plr;
}


