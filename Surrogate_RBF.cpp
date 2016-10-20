#include "Surrogate_RBF.hpp"


/*----------------------------*/
/*         constructor        */
/*----------------------------*/
SGTELIB::Surrogate_RBF::Surrogate_RBF ( SGTELIB::TrainingSet & trainingset,
                                        SGTELIB::Surrogate_Parameters param) :
  SGTELIB::Surrogate ( trainingset , param ),
  _q                 ( -1                  ),
  _qrbf              ( -1                  ),
  _qprs              ( -1                  ),
  _H                 ( "H",0,0             ),
  _HtH               ( "HtH",0,0           ),
  _HtZ               ( "HtZ",0,0           ),
  _Ai                ( "Ai",0,0            ),
  _ALPHA             ( "alpha",0,0         ),
  _selected_kernel   (1,-1                 ){
  #ifdef SGTELIB_DEBUG
    std::cout << "constructor RBF\n";
  #endif
}//

/*----------------------------*/
/*          destructor        */
/*----------------------------*/
SGTELIB::Surrogate_RBF::~Surrogate_RBF ( void ) {

}//


/*----------------------------*/
/*          display           */
/*----------------------------*/
void SGTELIB::Surrogate_RBF::display_private ( std::ostream & out ) const {
  out << "_q: " << _q << "\n";
  out << "_qrbf: " << _qrbf << "\n";
  out << "_qprs: " << _qprs << "\n";
  out << "_kernel_coef: " << _param.get_kernel_coef() << "\n";
  out << "_ridge: " << _param.get_ridge() << "\n";
}//


/*--------------------------------------*/
/*             init_private            */
/*--------------------------------------*/
bool SGTELIB::Surrogate_RBF::init_private ( void ) {

  #ifdef SGTELIB_DEBUG
    std::cout << "Surrogate_RBF : init_private\n";
  #endif

  if (_trainingset.get_pvar()<3){
    // Not enough points
    return false;
  }

  // Check preset
  const std::string preset = _param.get_preset();
  const bool modeO = string_find(preset,"O");
  const bool modeR = string_find(preset,"R");
  const bool modeI = string_find(preset,"I");
  if (modeO+modeR+modeI!=1){
    throw SGTELIB::Exception ( __FILE__ , __LINE__ ,
    "RBF preset must contain either \"O\", \"R\" or \"I\", exclusively." );
  }

  if (modeI){
    // Select Incomplete basis
    const int nvar = _trainingset.get_nvar();
    _qrbf = std::min(std::max(_p/2,3),100*nvar);
    _selected_kernel.clear();
    _selected_kernel = _trainingset.select_greedy( get_matrix_Xs(),
                                                   _trainingset.get_i_min(),
                                                   _qrbf,
                                                   1.0,
                                                   _param.get_distance_type());
  }
  else{
    _qrbf = _trainingset.get_pvar();
  }

  // Number of PRS basis functions
  if (modeO){
    const int dmin = SGTELIB::kernel_dmin(_param.get_kernel_type());
    if      (dmin==-1) _qprs = 0;
    else if (dmin==0 ) _qprs = 1;
    else if (dmin==1 ) _qprs = 1 + _trainingset.get_nvar(); 
    else{
      std::cout << "dmin = " << dmin << "\n";
      throw SGTELIB::Exception ( __FILE__ , __LINE__ ,"dmin out of range." );
    }
  }
  else{
    // For modes R and I, use n+1 prs basis functions.
    _qprs = 1 + _trainingset.get_nvar();
  }

  // Total number of basis function
  _q = _qrbf + _qprs;

  return true;
}//


/*--------------------------------------*/
/*               build                  */
/*--------------------------------------*/
bool SGTELIB::Surrogate_RBF::build_private ( void ) {

  // The build mainly consists of computing alpha  

  // Compute scaling distance for each training point
  const int pvar = _trainingset.get_pvar();
  const SGTELIB::Matrix & Zs = get_matrix_Zs();

  if ( string_find(_param.get_preset(),"O") ){
    // =========================================
    // Solve with orthogonality constraints
    // =========================================
    // Build design matrix with constraints lines
    _H = compute_design_matrix(get_matrix_Xs(),true); 
    // Inverte matrix
    _Ai = _H.lu_inverse();
    // Product (only the p first rows of Ai)
    _ALPHA = SGTELIB::Matrix::subset_product(_Ai,Zs,-1,pvar,-1);
  }
  else{
    // =========================================
    // Solve with ridge coefficient
    // =========================================

    // Build design matrix WITHOUT constraints lines
    _H = compute_design_matrix(get_matrix_Xs(),false);
    _HtH = SGTELIB::Matrix::transposeA_product(_H,_H);
    _HtZ = SGTELIB::Matrix::transposeA_product(_H,get_matrix_Zs());
    SGTELIB::Matrix A = _HtH;

    const double r = _param.get_ridge();
  
    // Add regularization term
    if ( string_find(_param.get_preset(),"1") ){
      // Add ridge to all basis function
      for (int i=0 ; i<_q ; i++) A.add(i,i,r);
    }
    else if ( string_find(_param.get_preset(),"2") ){
      // Add ridge to all basis function except constant
      for (int i=0 ; i<_q-1 ; i++) A.add(i,i,r);
    }
    else if ( string_find(_param.get_preset(),"3") ){
      // Add ridge to all radial basis function
      for (int i=0 ; i<_qrbf ; i++) A.add(i,i,r);
    }
    else {
      // Add ridge to all radial basis function (Same as R3)
      for (int i=0 ; i<_qrbf ; i++) A.add(i,i,r);
    }
    _Ai = A.cholesky_inverse();
    _ALPHA = _Ai*_HtZ;
  }

  // Check for Nan  
  if (_ALPHA.has_nan()){
    return false;
  }

  _ready = true;
  return true;    
  
}//





/*--------------------------------------*/
/*         Compute Design matrix        */
/*--------------------------------------*/
const SGTELIB::Matrix SGTELIB::Surrogate_RBF::compute_design_matrix ( const SGTELIB::Matrix & XXs , const bool constraints ) {

  // Xs can be, either the training set, to build the model, or prediction points.
  // To build the model, we need the orthogonality constraints, so the 2nd arg is going to be true.
  // For prediction, constraints = false.

  const int pxx = XXs.get_nb_rows();

  // Get the distance from each input point XXs to each kernel
  SGTELIB::Matrix H = _trainingset.get_distances(XXs,get_matrix_Xs().get_rows(_selected_kernel),_param.get_distance_type());
  // Apply kernel values
  /*
  for ( int i=0 ; i<_qrbf ; i++ ){
    H.set_col(kernel(_param.get_kernel_type(),_kernel_coef,H.get_col(i)),i);
  }
  */
  H = kernel(_param.get_kernel_type(),_param.get_kernel_coef(),H);

  // If there are some PRS basis functions
  if (_qprs>0){

    // Build the matrix that contains linear terms and the constant
    SGTELIB::Matrix L ("L",pxx,_qprs);
    int k = 0;

    // First columns are the linear terms
    if (_qprs>1){
      for ( int j=0 ; j<_n ; j++){
        if (_trainingset.get_X_nbdiff(j)>1){
          L.set_col ( XXs.get_col(j) , k++ );
        }
      } 
    }

    // Last column is the constant term
    L.set_col( 1.0 , k );

    // Concatenate with the RBF terms
    H.add_cols(L);

    if (constraints){
      // The constraints are the transpose of L
      L = L.transpose();
      // And we add some zeros for the right bottom part of the matrix
      L.add_cols(_qprs);
      // We add this to the design matrix
      H.add_rows(L);
    }
  
  }

  return H;
}//




/*--------------------------------------*/
/*       predict (ZZs only)             */
/*--------------------------------------*/
void SGTELIB::Surrogate_RBF::predict_private ( const SGTELIB::Matrix & XXs,
                                                     SGTELIB::Matrix * ZZs) {
  check_ready(__FILE__,__FUNCTION__,__LINE__);
  *ZZs = compute_design_matrix(XXs,false) * _ALPHA;
}//

/*--------------------------------------*/
/*       get matrix Zvs                 */
/*--------------------------------------*/
const SGTELIB::Matrix * SGTELIB::Surrogate_RBF::get_matrix_Zvs (void){
  check_ready(__FILE__,__FUNCTION__,__LINE__);
  if (not _Zvs){

    // Init _Zvs
    _Zvs = new SGTELIB::Matrix;
    const SGTELIB::Matrix & Zs = get_matrix_Zs();

    if ( string_find(_param.get_preset(),"O") ){
      //============================================
      // ORTHOGONALITY CONSTRAINTS
      //============================================
      SGTELIB::Matrix dAiAlpha = SGTELIB::Matrix::diagA_product(_Ai.diag_inverse(),_ALPHA);
      dAiAlpha.remove_rows(_qprs);
      *_Zvs = Zs-dAiAlpha;
    }
    else{
      //SGTELIB::Matrix dPiPZs    = SGTELIB::Matrix::get_matrix_dPiPZs(_Ai,_H,Zs);
      SGTELIB::Matrix dPiPZs = SGTELIB::Matrix::get_matrix_dPiPZs(_Ai,_H,Zs,_ALPHA);
    
      // dPi is the inverse of the diag of P 
      // Compute _Zv = Zs - dPi*P*Zs
      *_Zvs = Zs - dPiPZs;
    }

    _Zvs->replace_nan(+INF);
    _Zvs->set_name("Zvs");

  }
  return _Zvs;
}//


/*--------------------------------------*/
/*       get bumpiness                  */
/*--------------------------------------*/
/*
SGTELIB::Matrix SGTELIB::Surrogate_RBF::get_bumpiness (void){
  // aj is the set of coefficients for the output j
  SGTELIB::Matrix aj;
  // Y is a buffer.
  SGTELIB::Matrix Y;
  SGTELIB::Matrix bumpiness ("B",1,_m);
  for (int j=0 ; j<_m ; j++){
    aj = _ALPHA.get_col(j);
    Y = SGTELIB::Matrix::subset_product(_H,aj,_qrbf,_qrbf,1);
    Y = SGTELIB::Matrix::subset_product(aj.transpose(),Y,1,_qrbf,1);
    bumpiness.set(0,j,Y.get(0));
  }
  // TODO: add sign (-1)^dmin
  return bumpiness;

}//
*/

