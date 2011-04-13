#include "CartesianGeneratorPos.hpp"
#include <ocl/Component.hpp>

ORO_CREATE_COMPONENT( trajectory_generators::CartesianGeneratorPos);

namespace trajectory_generators
{

    using namespace RTT;
    using namespace KDL;
    using namespace std;

    CartesianGeneratorPos::CartesianGeneratorPos(string name)
        : TaskContext(name,PreOperational)
    {
    	PI = 3.141592;
        //Creating TaskContext

    	is_moving = false;
    	toROS = true;
    	lastCommndedPoseJntPos = std::vector<double>(7,0.0);
    	v_max = std::vector<double>(7,0.5);
    	a_max = std::vector<double>(7,0.5);
    	jntVel = std::vector<double>(7,0.0);
    	num_axes = 7;

        //Adding InputPorts
        this->addPort("CartesianPoseDes",cmd_cartPosPort);
        this->addPort("JointPositionMsr",msr_jntPosPort);

        //Adding OutputPorts
        this->addPort("JointPositionDes",cmd_jntPosPort);
        this->addPort("JointPositionDesToROS",cmd_jntPosPort_toROS);
        //Adding Properties
        //this->addProperty("max_vel",v_max).doc("Maximum Velocity in Trajectory");
        //this->addProperty("max_acc",a_max).doc("Maximum Acceleration in Trajectory");
        //this->addProperty("num_axes",num_axes).doc("Number of Axes");

        this->addOperation("moveTo",&CartesianGeneratorPos::moveTo,this,OwnThread)
        	  .doc("Set the position setpoint")
        	  .arg("setpoint", "position setpoint for end effector")
        	  .arg("time", "minimum time to execute trajectory" );

        //Adding Methods
        this->addOperation( "resetPosition",&CartesianGeneratorPos::resetPosition,this,OwnThread).doc("Reset generator's position" );

        jntState.header.frame_id = "arm_0_link";
        jntState.name.push_back("arm_1_joint");
        jntState.name.push_back("arm_2_joint");
        jntState.name.push_back("arm_3_joint");
        jntState.name.push_back("arm_4_joint");
        jntState.name.push_back("arm_5_joint");
        jntState.name.push_back("arm_6_joint");
        jntState.name.push_back("arm_7_joint");

    }

    CartesianGeneratorPos::~CartesianGeneratorPos()
    {
    }

    bool CartesianGeneratorPos::configureHook()
    {
    	//num_axes = num_axes_prop.rvalue();
    	//TODO:check if dimensions of v_max and a_max match num_axes

    	//v_max = v_max_property.rvalue();
    	//a_max = a_max_property.rvalue();

    	log(Info) << "nAxes = " << num_axes << endlog();

    	return true;

    }

    bool CartesianGeneratorPos::startHook()
    {
    	return true;
    }

    void CartesianGeneratorPos::updateHook()
    {
    	time_passed = os::TimeService::Instance()->secondsSince(time_begin);
    	if(cmd_cartPosPort.read(lastCommandedPose) == RTT::NewData){
    		log(Info) << "a new Pose arrived from ROS" << endlog();
    		cout << "a new Pose arrived from ROS" << endl;
    		//Do IK and reset velocity profiles
    		this->ikSolver(lastCommandedPose, lastCommndedPoseJntPos);
    		//Create joint specific velocity profiles
    		double maxDuration = 0.0;
    		std::vector<double> jntPos;
    		jntPos = std::vector<double>(7,0.0);
    		//msr_jntPosPort.read(jntPos);
    		if ((int)motionProfile.size() == 0){//Only for the first run
    			for(int i = 0; i < (int)num_axes; i++)
    				jntVel[i] = 0.0;
    		}else{
    			for(int i = 0; i < (int)motionProfile.size(); i++)
    				jntVel[i] = motionProfile[i].getVel(time_passed);
    		}

    		motionProfile.clear();
    		//TODO: Check dimensions
    		for(int i = 0; i < (int)lastCommndedPoseJntPos.size(); i++){
    			motionProfile.push_back(VelocityProfile_NonZeroInit(a_max[i], v_max[i], lastCommndedPoseJntPos[i], jntPos[i], jntVel[i]));
    			//TODO: Add non zero velocities.
    			if(motionProfile[i].getDuration() > maxDuration )
    				maxDuration = motionProfile[i].getDuration();
    		}
    		//Do sync
    		for(int i = 0; i < (int)lastCommndedPoseJntPos.size(); i++){
    			motionProfile[i].setDuration(maxDuration);
    		}

    		//Set times
    		time_begin = os::TimeService::Instance()->getTicks();
    		time_passed = 0.0;
    		log(Info) << "A new set of motion profiles were successfully created." << endlog();

    	}else{
    		//Execute current velocity profile

    		if (motionProfile.size()==7){
    			jntState.position.clear();
    			jntPosCmd.clear();
    			for(int i = 0; i < (int)motionProfile.size(); i++){
    				jntPosCmd.push_back(motionProfile[i].getPos(time_passed));
    				if(toROS){jntState.position.push_back(motionProfile[i].getPos(time_passed));}
    			}
    			cmd_jntPosPort.write(jntPosCmd);
    			if(toROS){cmd_jntPosPort_toROS.write(jntState);}
    		}

    	}
    }

    void CartesianGeneratorPos::stopHook()
    {
    }

    void CartesianGeneratorPos::cleanupHook()
    {
    }

    bool CartesianGeneratorPos::moveTo(std::vector<double> position, double time)
    {
	  return true;
    }

    void CartesianGeneratorPos::resetPosition()
    {
    	//This function is really not necessary since we are currently running in command mode 1: Joint Position Control Mode
    }

    bool CartesianGeneratorPos::ikSolver(geometry_msgs::Pose & poseDsr, std::vector<double> & jntPosDsr){
    	//TODO: Check size of jntPosDsr. Should be 7
    	double a2 = 0.4, a3 = 0.39, d6 = 0.078;
    	double mod_pW, mod_pWxy, c2, s2, c3, s3;

    	KDL::Rotation R = KDL::Rotation::Quaternion(poseDsr.orientation.x,poseDsr.orientation.y,poseDsr.orientation.z,poseDsr.orientation.w);
    	KDL::Vector p(poseDsr.position.x, poseDsr.position.y, poseDsr.position.z);

    	// wrist position
    	KDL::Vector pW = p - d6 * R.UnitZ();

    	//Q1
    	jntPosDsr[0] = std::atan2(pW[1], pW[0]);
    	mod_pW = pow(pW.Norm(),2);

    	c3 = (mod_pW - a2*a2 - a3*a3)/(2*a2*a3);
    	s3 = -sqrt(1-c3*c3);
    	jntPosDsr[3] = atan2(s3,c3)+PI/2;

    	jntPosDsr[2] = 0.0;

    	mod_pWxy = sqrt(pW[0]*pW[0] + pW[1]*pW[1]);
    	s2 = ((a2+a3*c3)*pW[2] - a3*s3*mod_pWxy)/mod_pW;
    	c2 = ((a2+a3*c3)*mod_pWxy + a3*s3*pW[2])/mod_pW;
    	jntPosDsr[1] = atan2(s2,c2);

    	KDL::Rotation T01(cos(jntPosDsr[0]), 0.0, sin(jntPosDsr[0]), sin(jntPosDsr[0]), 0.0, -cos(jntPosDsr[0]), 0.0, 1.0, 0.0);
    	KDL::Rotation T12(cos(jntPosDsr[1]), -sin(jntPosDsr[1]), 0.0, sin(jntPosDsr[1]), cos(jntPosDsr[1]), 0.0, 0.0 , 0.0, 1.0);
    	KDL::Rotation T23(cos(jntPosDsr[3]), 0.0, sin(jntPosDsr[3]), sin(jntPosDsr[3]), 0.0, -cos(jntPosDsr[3]), 0.0, 1.0, 0.0);

    	KDL::Rotation pose03 = T01*T12*T23;
    	KDL::Rotation pose36 = pose03.Inverse() * R;

    	jntPosDsr[4] = atan2(pose36(1,2),pose36(0,2));
    	jntPosDsr[5] = atan2(sqrt(pose36(0,2)*pose36(0,2) + pose36(1,2)*pose36(1,2)), pose36(2,2));
    	jntPosDsr[6] = atan2(pose36(2,1),-pose36(2,0));

    	//Adjust to robot from IK coordinates
    	jntPosDsr[1] -= PI/2;
    	jntPosDsr[3] -= PI/2;
    	jntPosDsr[6] -= PI;


    	for(int i=0; i < (int)jntPosDsr.size(); i++){
    		cout << i << ":" << jntPosDsr[i] << endl;
    	}

    	return true;
    }
}//namespace


