#include <quadruped.h>
#include <utilities.h>
// -----------------------------------------------------------------------------
using namespace Ravelin;
//using namespace Moby;

extern Ravelin::VectorNd STAGE1, STAGE2;
extern int N_SYSTEMS;
std::map<std::string , std::vector<double> > gait;

extern std::vector<double> unknown_base_perturbation;
extern std::vector<double> known_base_perturbation;

// TODO: This should be extern double to moby's (nominal) STEP_SIZE

extern bool new_sim_step;

// ============================================================================
// ======================= USER DEFINED PARAMETERS ============================

#ifdef VISUALIZE_MOBY
# include <OpenGL/gl.h>
# include <OpenGL/glu.h>
# include <GLUT/glut.h>
GLConsole theConsole;
#endif

static bool
        WALK                = true,//"Activate Walking?"),
          TRACK_FOOTHOLDS     = false,//"Locate and use footholds?"),// EXPERIMENTAL
        TRUNK_STABILIZATION = false,  // EXPERIMENTAL
        CONTROL_IDYN        = true,//"Activate IDYN?"),
          WORKSPACE_IDYN      = false,//"Activate WIDYN?"),// EXPERIMENTAL
          USE_LAST_CFS        = false,//"Use last detected contact forces?"),// EXPERIMENTAL
        FRICTION_EST        = false,  // EXPERIMENTAL
        ERROR_FEEDBACK      = true,//"Use error-feedback control?"),
          FEEDBACK_FORCE      = false,//"Apply error-feedback as forces?"),
          FEEDBACK_ACCEL      = true,//"Apply error-feedback as accelerations?"),
          WORKSPACE_FEEDBACK  = true;//"Use error-feedback in workspace frame?");

// -- LOCOMOTION OPTIONS --
double
        gait_time   = 0.5,//,"Gait Duration over one cycle."),
        step_height = 0.02,//,""),
        goto_X      = 0.05,//,"command forward direction"),
        goto_Y      = 0.00,//,"command lateral direction"),
        goto_GAMMA  = 0.0;//,"command rotation");

// Assign Gait to the locomotion controller
std::string
        gait_type   = "trot"; //,"Gait type [trot,walk,pace,bount,rgallop,tgallop]");

std::vector<double>
        duty_factor = std::vector<double>(),
        goto_command = std::vector<double>();

// -- IDYN OPTIONS --
double
        STEP_SIZE = 0.01;

// ============================================================================
// ============================================================================

Ravelin::VectorNd& Quadruped::control(double t,
                                      const Ravelin::VectorNd& q,
                                      const Ravelin::VectorNd& qd,
                                      Ravelin::VectorNd& q_des,
                                      Ravelin::VectorNd& qd_des,
                                      Ravelin::VectorNd& u){
  static Ravelin::VectorNd qd_last = qd;
  std::cerr << " -- Quadruped::control(.) entered" << std::endl;

  OUT_LOG(logINFO)<< "time = "<< t ;

  // Subtract unknown perturbations from fext vector
  for(int i=0;i<6;i++)
    fext[NUM_JOINTS+i] -= unknown_base_perturbation[i];

  ((qdd = qd)-=qd_last)/=STEP_SIZE;

#ifdef PERTURB_CONTACT_DATA
  static std::normal_distribution<double> distribution_normal(0,0.5);
  static std::normal_distribution<double> distribution_point(0,0.01);

  for(int i=0;i<0;i++){ // NOTE: Change this to 4 if you want to disrupt perfect contact data
    if(!eefs_[i].active)
      continue;
    // PERTURB NORMAL
    eefs_[i].normal += Ravelin::Vector3d(distribution_normal(generator),distribution_normal(generator),0);
    eefs_[i].normal.normalize();
    // PERTURB POINT
    eefs_[i].point += Ravelin::Vector3d(distribution_point(generator),distribution_point(generator),distribution_point(generator));
#  ifdef VISUALIZE_MOBY
      visualize_ray(eefs_[i].point,eefs_[i].point+eefs_[i].normal*0.05,Ravelin::Vector3d(1,1,0),sim);
#  endif
  }

#  ifdef COLLECT_DATA   // record all input vars
 {
   // generate a unique filename
   std::ostringstream fname;
   fname << "data/true_data" << (N_SYSTEMS) << ".m";
   // open the file
   std::ofstream out(fname.str().c_str());
   out << "contacts = [";
   for(unsigned i=0;i< eefs_.size();i++){
     if(eefs_[i].active)
       out << 1 << " "<< std::endl;
     else
       out << 0 << " " << std::endl;
   }
   out << "];" << std::endl;
   for(unsigned i=0,ii=0;i< eefs_.size();i++){
      if(!eefs_[i].active){
        out << "impulse_" << i << " = [0,0,0];" << std::endl;
        out << "normal_"  << i << " = [0,0,0];" << std::endl;
        out << "point_"   << i << " = [0,0,0];" << std::endl;
        continue;
      }
      out << "impulse_" << i << " = "<< eefs_[i].contact_impulses[0] << ";" << std::endl;
      out << "normal_"  << i << " = "<< eefs_[i].normal << ";" << std::endl;
      out << "point_"   << i << " = "<< eefs_[i].point << ";" << std::endl;
      ii++;
    }
      out <<"impulse_true = [impulse_0' impulse_1' impulse_2' impulse_3'];" << std::endl;
      out <<"normal_true  = [normal_0' normal_1' normal_2' normal_3'];" << std::endl;
      out <<"point_true   = [point_0' point_1' point_2' point_3'];" << std::endl;

      out.close();
    }
#  endif
#endif

  update();
  OUT_LOG(logINFO)<< "num_contacts = " << NC ;

  uff.set_zero(NUM_JOINTS);
  ufb.set_zero(NUM_JOINTS);
  u.set_zero(NUM_JOINTS);

  qdd_des.set_zero(NUM_JOINTS);
  qd_des.set_zero(NUM_JOINTS);
  q_des.set_zero(NUM_JOINTS);

  for(unsigned i=0;i< NUM_JOINTS;i++){
    joints_[i]->q[0]  = q[i];
    joints_[i]->qd[0]  = qd[i];
  }
  abrobot_->update_link_poses();
  abrobot_->update_link_velocities();

  q_des = q;
  Ravelin::SVector6d go_to(0,0,0,0,0,0,base_frame);
  Ravelin::VectorNd vb_w(NUM_EEFS*3 + 6);
  std::vector<Ravelin::Vector3d> foot_vel(NUM_EEFS), foot_pos(NUM_EEFS), foot_acc(NUM_EEFS);

  for(unsigned i=0;i< NUM_EEFS;i++){
    foot_pos[i] = Ravelin::Pose3d::transform_point(base_frame,Ravelin::Vector3d(0,0,0,eefs_[i].link->get_pose()));
    foot_vel[i].set_zero();
    foot_acc[i].set_zero();
    foot_pos[i].pose = foot_vel[i].pose = foot_acc[i].pose = base_frame;
  }

  if(WALK){
    // FOOTHOLDS
    static std::vector<Ravelin::Vector3d> footholds;
    if(TRACK_FOOTHOLDS){
      // Determine new footholds (every 10th of a second)
      if(((int)(t*1000) % 100) == 0){
        find_footholds(footholds,1000);
      }
    } else {
      footholds.clear();
    }

    // Foot Locations
    std::vector<Ravelin::Vector3d> foot_origin;
    for(unsigned i=0;i< NUM_EEFS;i++)
      foot_origin.push_back(eefs_[i].origin);

    for(int i=0;i<6;i++)
      go_to[i] = goto_command[i];

    OUTLOG(gait[gait_type],gait_type,logINFO);
    OUTLOG(duty_factor,"duty_factor",logINFO);

    walk_toward(go_to,gait[gait_type],footholds,duty_factor,gait_time,step_height,foot_origin,t,q,qd,qdd,foot_pos,foot_vel, foot_acc);
    for(int i=0;i<NUM_EEFS;i++){
      OUT_LOG(logDEBUG) << "\t" << eefs_[i].id << "_x =" << foot_pos[i];
      OUT_LOG(logDEBUG) << "\t" << eefs_[i].id << "_xd =" << foot_vel[i];
      OUT_LOG(logDEBUG) << "\t" << eefs_[i].id << "_xdd =" << foot_acc[i];
    }
  }
  else {
    for(int i=0;i<NUM_EEFS;i++){
      foot_pos[i] = eefs_[i].origin;
      foot_pos[i].pose = base_frame;
      RRMC(eefs_[i],q,eefs_[i].origin,q_des);
      Ravelin::VectorNd q_diff;
      (q_diff= q_des) -= q;
      q_diff *= STEP_SIZE;
      (q_des = q) += q_diff;
    }
  }
  trajectory_ik(foot_pos,foot_vel, foot_acc,q_des,qd_des,qdd_des);

  static Ravelin::MatrixNd MU;
  MU.set_zero(NC,NK/2);

  // -----------------------------------------------------------------------------
  // EXPERIMENTAL
  if(FRICTION_EST){
    Ravelin::VectorNd cf;
    double err = friction_estimation(vel,fext,STEP_SIZE,N,D,M,MU,cf);
    OUT_LOG(logINFO)<< "err (friction estimation): " << err << std::endl;
    OUTLOG(MU,"MU",logDEBUG);
    OUTLOG(cf,"contact_forces",logDEBUG);
  } else
    for(int i=0;i<NC;i++)
      if(eefs_[i].active)
        for(int k=0;k<NK/2;k++)
          MU(i,k) = /*100.0;//*/eefs_[i].event->contact_mu_coulomb;

  // -----------------------------------------------------------------------------
  // EXPERIMENTAL
  if(TRUNK_STABILIZATION){
    Ravelin::VectorNd id(NUM_JOINTS);
    Ravelin::MatrixNd J;
    calc_base_jacobian(J);
    zmp_stabilizer(J,Ravelin::Vector2d(0,0),id);
    qdd_des += id;
  }

  // -----------------------------------------------------------------------------

  if (ERROR_FEEDBACK){
    static std::map<std::string, Gains>      gains;
    if(FEEDBACK_ACCEL){
      // GAINS FOR ACCELERATION ERROR FEEDABCK
      for(int i=0;i<NUM_JOINTS;i++){
        gains[joints_[i]->id].perr_sum = 0;
        gains[joints_[i]->id].kp = 1e5;
        gains[joints_[i]->id].kv = 5e2;
        gains[joints_[i]->id].ki = 0;
      }
      PID::control(q_des, qd_des,q,qd,joint_names_, gains,qdd_des);
    }

    if(FEEDBACK_FORCE){
      // GAINS FOR FORCE ERROR FEEDABCK
      for(int i=0;i<NUM_JOINTS;i++){
        gains[joints_[i]->id].perr_sum = 0;
        gains[joints_[i]->id].kp = 1e1;
        gains[joints_[i]->id].kv = 1e-2;
        gains[joints_[i]->id].ki = 1e-3;
      }
      PID::control(q_des, qd_des,q,qd,joint_names_, gains,ufb);
    }

    if(WORKSPACE_FEEDBACK){
      std::vector<Ravelin::Matrix3d> W(boost::assign::list_of(Ravelin::Matrix3d::identity())(Ravelin::Matrix3d::identity())(Ravelin::Matrix3d::identity())(Ravelin::Matrix3d::identity()).convert_to_container<std::vector<Ravelin::Matrix3d> >() );
      // CURRENTLY THIS IS ONLY FORCE
      // BUT IT CAN BE ACCELERATIONS TOO
      eef_stiffness_fb(W,foot_pos,foot_vel,q,qd,ufb);
    }
  }

  // -----------------------------------------------------------------------------

  if(CONTROL_IDYN){
    double dt = STEP_SIZE;
    double alpha = 1.0;
    Ravelin::VectorNd cf;
    Ravelin::VectorNd id = Ravelin::VectorNd::zero(NUM_JOINTS);

    if(USE_LAST_CFS){
      cf.set_zero(NC*5);
      for(unsigned i=0,ii=0;i< eefs_.size();i++){
        if(!eefs_[i].active) continue;
          Ravelin::Matrix3d R_foot(             eefs_[i].normal[0],              eefs_[i].normal[1],              eefs_[i].normal[2],
                                 eefs_[i].event->contact_tan1[0], eefs_[i].event->contact_tan1[1], eefs_[i].event->contact_tan1[2],
                                 eefs_[i].event->contact_tan2[0], eefs_[i].event->contact_tan2[1], eefs_[i].event->contact_tan2[2]);
        Ravelin::Origin3d contact_impulse = Ravelin::Origin3d(R_foot.mult(eefs_[i].contact_impulses[0],workv3_)*(STEP_SIZE/0.001));
        cf[ii] = contact_impulse[0];
        if(contact_impulse[1] > 0)
          cf[NC+ii] = contact_impulse[1];
        else
          cf[NC+ii+NC*2] = contact_impulse[1];
        if(contact_impulse[2] > 0)
          cf[NC+ii+NC] = contact_impulse[2];
        else
          cf[NC+ii+NC*3] = contact_impulse[2];
        ii++;
      }
      Utility::check_finite(cf);
      OUTLOG(cf,"cf z",logERROR);
    }

    if(WORKSPACE_IDYN){
      // EXPERIMENTAL
      Rw.mult(vel,vel_w);
      workspace_trajectory_goal(go_to,foot_pos,foot_vel,foot_acc,0.01,STEP_SIZE,vb_w);

      workspace_inverse_dynamics(vel_w,vb_w,M,fext,dt,MU,id,cf);
    } else {
      inverse_dynamics(vel,qdd_des,M,N,D,fext,dt,MU,id,cf);
    }

#ifdef PERTURB_CONTACT_DATA
#  ifdef COLLECT_DATA   // record all input vars
    {
      // generate a unique filename
      std::ostringstream fname;
      fname << "data/observed_data" << (N_SYSTEMS) << ".m";

      // open the file
      std::ofstream out(fname.str().c_str());

      out << "contacts = [";
      for(unsigned i=0;i< eefs_.size();i++){
        if(eefs_[i].active)
          out << 1 << " "<< std::endl;
        else
          out << 0 << " " << std::endl;
      }
      out << "];" << std::endl;

      for(unsigned i=0,ii=0;i< eefs_.size();i++){
        if(!eefs_[i].active){
          out << "impulse_" << i << " = [0,0,0];" << std::endl;
          out << "normal_"  << i << " = [0,0,0];" << std::endl;
          out << "point_"   << i << " = [0,0,0];" << std::endl;
          continue;
        }
        Ravelin::Matrix3d R_foot(             eefs_[i].normal[0],              eefs_[i].normal[1],              eefs_[i].normal[2],
                                 eefs_[i].event->contact_tan1[0], eefs_[i].event->contact_tan1[1], eefs_[i].event->contact_tan1[2],
                                 eefs_[i].event->contact_tan2[0], eefs_[i].event->contact_tan2[1], eefs_[i].event->contact_tan2[2]);
        Ravelin::Origin3d contact_impulse(cf[ii],(cf[ii*NK+NC]-cf[ii*NK+NC+NK/2]),(cf[ii*NK+NC+1]-cf[ii*NK+NC+NK/2+1]));
        out << "impulse_" << i << " = "<< R_foot.transpose_mult(contact_impulse,workv3_)/(STEP_SIZE/0.001) << ";" << std::endl;
        out << "normal_"  << i << " = "<< eefs_[i].normal << ";" << std::endl;
        out << "point_"   << i << " = "<< eefs_[i].point << ";" << std::endl;
        OUTLOG(eefs_[i].normal,eefs_[i].id + "_normal",logERROR);
#    ifdef VISUALIZE_MOBY
        visualize_ray(eefs_[i].point+eefs_[i].normal,eefs_[i].point,Ravelin::Vector3d(1,1,0),sim);
#    endif
        ii++;
      }

      out <<"impulse_observed = [impulse_0' impulse_1' impulse_2' impulse_3'];" << std::endl;
      out <<"normal_observed  = [normal_0' normal_1' normal_2' normal_3'];" << std::endl;
      out <<"point_observed   = [point_0' point_1' point_2' point_3'];" << std::endl;

      out.close();
    }
#  endif
#endif

    uff += (id*=alpha);
  }

  // -----------------------------------------------------------------------------

  Utility::check_finite(ufb);
  Utility::check_finite(uff);
  // combine ufb and uff
  u = ufb;
  u += uff;

  // -----------------------------------------------------------------------------

  for(unsigned i=0;i< NUM_JOINTS;i++){
    joints_[i]->q[0]  = q[i];
    joints_[i]->qd[0]  = qd[i];
  }
  abrobot_->update_link_poses();
  abrobot_->update_link_velocities();

     ((workv_ = qd)-=qd_last)/=STEP_SIZE;
     OUT_LOG(logINFO) <<"JOINT\t: U\t| Q\t: des\t: err\t|Qd\t: des\t: err\t|Qdd\t: des\t: err"<<std::endl;
     for(unsigned i=0;i< NUM_JOINTS;i++)
       OUT_LOG(logINFO)<< joints_[i]->id
                 << "\t " <<  std::setprecision(4) << u[i]
                 << "\t| " << joints_[i]->q[0]
                 << "\t " << q_des[i]
                 << "\t " << q[i] - q_des[i]
                 << "\t| " << joints_[i]->qd[0]
                 << "\t " << qd_des[i]
                 << "\t " <<  qd[i] - qd_des[i]
                 << "\t| " << qdd[i]
                 << "\t " << qdd_des[i]
                 << "\t " <<  (qdd[i] - qdd_des[i]);
     OUTLOG(roll_pitch_yaw,"roll_pitch_yaw",logINFO);
     OUTLOG(zero_moment_point,"ZmP",logINFO);
     OUTLOG(center_of_mass_x,"CoM_x",logINFO);
     OUTLOG(center_of_mass_xd,"CoM_xd",logINFO);
     OUTLOG(center_of_mass_xdd,"CoM_xdd",logINFO);
     OUTLOG(q,"q",logDEBUG);
     OUTLOG(qd,"qd",logDEBUG);
     OUTLOG(qdd,"qdd",logDEBUG);
     OUTLOG(q_des,"q_des",logDEBUG);
     OUTLOG(qd_des,"qd_des",logDEBUG);
     OUTLOG(qdd_des,"qdd_des",logDEBUG);
     OUTLOG(fext,"fext",logDEBUG);

     OUTLOG(uff,"uff",logDEBUG);
     OUTLOG(ufb,"ufb",logDEBUG);
   // -----------------------------------------------------------------------------

   // Deactivate all contacts
   NC = 0;
   for(int i=0;i<eefs_.size();i++)
     eefs_[i].active = false;

   qd_last = qd;
   std::cerr << " -- Quadruped::control(.) exited" << std::endl;

   return u;
}
#ifdef VISUALIZE_MOBY
# include <thread>
  extern void init_glconsole();
  std::thread * tglc;
#endif

void Quadruped::init(){
#ifdef VISUALIZE_MOBY
  CVarUtils::AttachCVar( "qd.known_base_perturbation",&known_base_perturbation,"Apply a constant [3 linear,3 angular] force to robot base, the robot can sense the applied force");
  CVarUtils::AttachCVar( "qd.unknown_base_perturbation",&unknown_base_perturbation,"Apply a constant [3 linear,3 angular] force to robot base, the robot can NOT sense the applied force");

  CVarUtils::AttachCVar( "qd.locomotion.active",&WALK,"Activate Walking?");
  CVarUtils::AttachCVar( "qd.locomotion.track_footholds",&TRACK_FOOTHOLDS,"Locate and use footholds?");// EXPERIMENTAL
  CVarUtils::AttachCVar( "qd.idyn",&CONTROL_IDYN,"Activate IDYN?");
  CVarUtils::AttachCVar( "qd.widyn",&WORKSPACE_IDYN,"Activate WIDYN?");// EXPERIMENTAL
  CVarUtils::AttachCVar( "qd.use_cfs",&USE_LAST_CFS,"Use last detected contact forces?");// EXPERIMENTAL
  CVarUtils::AttachCVar( "qd.error-feedback.active",&ERROR_FEEDBACK,"Use error-feedback control?");
  CVarUtils::AttachCVar( "qd.error-feedback.force",&FEEDBACK_FORCE,"Apply error-feedback as forces?");
  CVarUtils::AttachCVar( "qd.error-feedback.accel",&FEEDBACK_ACCEL,"Apply error-feedback as accelerations?");
  CVarUtils::AttachCVar( "qd.error-feedback.workspace",&WORKSPACE_FEEDBACK,"Use error-feedback in workspace frame?");

  // -- LOCOMOTION OPTIONS --
  CVarUtils::AttachCVar( "qd.locomotion.gait_time",&gait_time,"Gait Duration over one cycle.");
  CVarUtils::AttachCVar( "qd.locomotion.step_height",&step_height,"Height of a step");
  CVarUtils::AttachCVar( "qd.locomotion.command",&goto_command,"Base command differential");

  // Assign Gait to the locomotion controller
  CVarUtils::AttachCVar<std::string>( "qd.locomotion.gait_type",&gait_type,"Gait type [trot,walk,pace,bount,rgallop,tgallop]");
  CVarUtils::AttachCVar< std::vector<double> >( "qd.locomotion.duty_factor",&duty_factor,"duty_factor");
  // -- IDYN OPTIONS --
  CVarUtils::AttachCVar( "qd.dt",&STEP_SIZE,"value for dt (also h) used in IDYN and other functions");

   tglc = new std::thread(init_glconsole);
#endif
  // Set up joint references
#ifdef FIXED_BASE
  NSPATIAL = 0;
  NEULER = 0;
#else
  NSPATIAL = 6;
  NEULER = 7;
#endif
  compile();

  // Set up end effectors
  eef_names_.push_back("LF_FOOT");
  eef_names_.push_back("RF_FOOT");
  eef_names_.push_back("LH_FOOT");
  eef_names_.push_back("RH_FOOT");

  int num_leg_stance = 4;
  switch(num_leg_stance){
    case 4:
      eef_origins_["LF_FOOT"] = Ravelin::Vector3d( 0.13, 0.096278, -0.16);
      eef_origins_["RF_FOOT"] = Ravelin::Vector3d( 0.13,-0.096278, -0.16);
      eef_origins_["LH_FOOT"] = Ravelin::Vector3d(-0.105, 0.096278, -0.16);
      eef_origins_["RH_FOOT"] = Ravelin::Vector3d(-0.105,-0.096278, -0.16);
    break;
    case 3:
      // NOTE THIS IS A STABLE 3-leg stance
      eef_origins_["LF_FOOT"] = Ravelin::Vector3d(0.18, 0.1275, -0.13);
      eef_origins_["RF_FOOT"] = Ravelin::Vector3d(0.14, -0.1075, -0.13);
      eef_origins_["LH_FOOT"] = Ravelin::Vector3d(-0.10, 0.06, -0.13);
      eef_origins_["RH_FOOT"] = Ravelin::Vector3d(-0.06, -0.04, -0.08);
      break;
    case 2:
      // NOTE THIS IS AN UNSTABLE 2-leg stance
      eef_origins_["LF_FOOT"] = Ravelin::Vector3d(0.14, 0.0775, -0.11);
      eef_origins_["RF_FOOT"] = Ravelin::Vector3d(0.14, -0.0775, -0.13);
      eef_origins_["LH_FOOT"] = Ravelin::Vector3d(-0.06, 0.07, -0.13);
      eef_origins_["RH_FOOT"] = Ravelin::Vector3d(-0.06, -0.04, -0.08);
      break;
    default: break;
  }

  NUM_JOINTS = joints_.size() - NUM_FIXED_JOINTS;
  NUM_LINKS = links_.size();
  NDOFS = NSPATIAL + NUM_JOINTS; // for generalized velocity, forces. accel

  OUT_LOG(logINFO)<< eef_names_.size() << " end effectors LISTED:" ;
  for(unsigned j=0;j<eef_names_.size();j++){
    for(unsigned i=0;i<links_.size();i++){
      if(eef_names_[j].compare(links_[i]->id) == 0){
        OUT_LOG(logINFO)<< eef_names_[j] << " FOUND!";
        eefs_.push_back(EndEffector(links_[i],eef_origins_[links_[i]->id],joint_names_));
        break;
      }
    }
  }

  NUM_EEFS = eefs_.size();
  OUT_LOG(logINFO)<< NUM_EEFS << " end effectors:" ;
  for(unsigned j=0;j<NUM_EEFS;j++){
    OUT_LOG(logINFO)<< eefs_[j].id ;
  }

  NK = 4;

  OUT_LOG(logINFO)<< "NUM_EEFS: " << NUM_EEFS ;
  OUT_LOG(logINFO)<< "N_FIXED_JOINTS: " << NUM_FIXED_JOINTS ;
  OUT_LOG(logINFO)<< "NUM_JOINTS: " << NUM_JOINTS ;
  OUT_LOG(logINFO)<< "NDOFS: " << NDOFS ;
  OUT_LOG(logINFO)<< "NSPATIAL: " << NSPATIAL ;
  OUT_LOG(logINFO)<< "NEULER: " << NEULER ;
  OUT_LOG(logINFO)<< "NK: " << NK ;

  q0_["BODY_JOINT"] = 0;
  q0_["LF_HIP_AA"] = M_PI_8;
  q0_["LF_HIP_FE"] = M_PI_4;
  q0_["LF_LEG_FE"] = M_PI_2;

  q0_["RF_HIP_AA"] =  -M_PI_8;
  q0_["RF_HIP_FE"] =  -M_PI_4;
  q0_["RF_LEG_FE"] =  -M_PI_2;

  q0_["LH_HIP_AA"] =  -M_PI_8;
  q0_["LH_HIP_FE"] =  -M_PI_4;
  q0_["LH_LEG_FE"] =  -M_PI_2;

  q0_["RH_HIP_AA"] =  M_PI_8;
  q0_["RH_HIP_FE"] =  M_PI_4;
  q0_["RH_LEG_FE"] =  M_PI_2;

  // Maximum torques
  std::map<std::string, double> torque_limits_;
  torque_limits_["BODY_JOINT"]=  2.60;
  torque_limits_["LF_HIP_AA"] =  2.60;
  torque_limits_["LF_HIP_FE"] =  2.60;
  torque_limits_["LF_LEG_FE"] =  2.60;

  torque_limits_["RF_HIP_AA"] =  2.60;
  torque_limits_["RF_HIP_FE"] =  2.60;
  torque_limits_["RF_LEG_FE"] =  2.60;

  torque_limits_["LH_HIP_AA"] =  2.60;
  torque_limits_["LH_HIP_FE"] =  6.00;
  torque_limits_["LH_LEG_FE"] =  2.60;

  torque_limits_["RH_HIP_AA"] =  2.60;
  torque_limits_["RH_HIP_FE"] =  6.00;
  torque_limits_["RH_LEG_FE"] =  2.60;

  // push into robot
  torque_limits_l.resize(NUM_JOINTS);
  torque_limits_u.resize(NUM_JOINTS);
  for(int i=0;i<NUM_JOINTS;i++){
    torque_limits_l[i] = -torque_limits_[joints_[i]->id];
    torque_limits_u[i] = torque_limits_[joints_[i]->id];
  }

  // Set Initial State
  Ravelin::VectorNd q_start(NUM_JOINTS+NEULER),
                    qd_start(NUM_JOINTS+NSPATIAL);

  abrobot_->get_generalized_coordinates(Moby::DynamicBody::eEuler,q_start);
  qd_start.set_zero();
  qd_start.set_zero();
  OUTLOG(q_start,"q_start",logINFO);

  for(unsigned i=0;i< NUM_JOINTS;i++)
    q_start[i] = (joints_[i]->q[0]  = q0_[joints_[i]->id]);
  OUTLOG(q_start,"q_start",logINFO);
  abrobot_->update_link_poses();
  update();

  for(int i=0;i<NUM_EEFS;i++){
    RRMC(eefs_[i],Ravelin::VectorNd(q_start),eefs_[i].origin,q_start);
    for(int j=0;j<eefs_[i].chain.size();j++){
      (joints_[eefs_[i].chain[j]]->q[0] = q_start[eefs_[i].chain[j]]);
      qd_start[eefs_[i].chain[j]] = 0;
    }
  }
  abrobot_->update_link_poses();

  {
    duty_factor = boost::assign::list_of(0.5)(0.5)(0.5)(0.5).convert_to_container<std::vector<double> >();
    goto_command = boost::assign::list_of(goto_X)(goto_Y)(0)(0)(0)(goto_GAMMA).convert_to_container<std::vector<double> >();
    // Trotting gait 50/50 duty cycle
    gait["trot"] = boost::assign::list_of(0.0)(0.5)(0.5)(0.0).convert_to_container<std::vector<double> >();

    // walk lf,rf,lh,rh
    gait["walk"] = boost::assign::list_of(0.25)(0.75)(0.0)(0.5).convert_to_container<std::vector<double> >();

    // pace
    gait["pace"] = boost::assign::list_of(0.0)(0.5)(0.0)(0.5).convert_to_container<std::vector<double> >();

    // bound
    gait["bound"] = boost::assign::list_of(0.5)(0.5)(0.0)(0.0).convert_to_container<std::vector<double> >();

    // transverse gallop
    gait["tgallop"] = boost::assign::list_of(0.8)(0.9)(0.3)(0.4).convert_to_container<std::vector<double> >();

    // Rotary gallop
    gait["rgallop"] = boost::assign::list_of(0.7)(0.6)(0.0)(0.1).convert_to_container<std::vector<double> >();
  }

  environment_frame = boost::shared_ptr<Ravelin::Pose3d>( new Ravelin::Pose3d(Moby::GLOBAL));
  environment_frame->x = Ravelin::Origin3d(0,0,0);
  environment_frame->q.set_identity();
}
