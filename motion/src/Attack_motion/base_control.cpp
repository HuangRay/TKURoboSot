#include "base_control.h"

//#include "../common/cssl/cssl.h"
serial_rx BaseControl::base_RX = {0,0,0,0,0,0};
struct timeval BaseControl::last_time = {0,0};
bool BaseControl::base_flag = false;
BaseControl::BaseControl()
{
//    this->base_robotCMD = {0, 0, 0, 0, 0, 0};
    this->base_robotFB = {0, 0, 0, 0, 0, 0};
    this->base_TX = {0xff,0xfa,0,0,0,0,0,0,0,0,0};
	this->serial = NULL;
#ifdef DEBUG
    std::cout << "BaseControl(DEBUG)\n";
#endif
#ifdef CSSL
	McsslInit();
#endif
    int p = pthread_create(&tid, NULL, (THREADFUNCPTR)&BaseControl::pThreadRun, this);
    if(p != 0){
        printf("base thread error\n");
        exit(EXIT_FAILURE);
    }
}

BaseControl::~BaseControl()
{
    if(this->serial){
	    McsslFinish();
        this->serial = NULL;
    }
#ifdef DEBUG
	std::cout << "~BaseControl(DEBUG)\n";
//    printf("tid : %d\n", tid);
//    printf("serial : %d\n", serial);
    std::cout << "tid: " << tid << std::endl;
    std::cout << "serial: " << serial << std::endl;
#endif
}

int BaseControl::McsslInit()
{
	std::cout << "==== Init cssl ====\n";
	cssl_start();
	if(!serial){
		serial = cssl_open(this->port, McsslCallback/*NULL*/, 0, 115200, 8, 0, 1);
	}
	if(!serial){
		std::cout << cssl_geterrormsg() << std::endl;
		std::cout << "===> ATTACK MOTION RS232 OPEN FAILED <===\n";
		fflush(stdout);
		//return 0;
		std::cout << this->port << std::endl;
		exit(EXIT_FAILURE);

	}else{
		std::cout << "----> ATTACK MOTION RS232 OPEN SUCCESSFUL <----\n";
		std::cout << "Initialize attack motion with port = "<< this->port << "...\n";
		cssl_setflowcontrol(serial, 0, 0);
	}
	return 1;
}

void BaseControl::McsslFinish()
{
    if(this->tid){
        pthread_cancel(tid);
        tid = NULL;
    }
#ifdef CSSL
	cssl_close(serial);
	cssl_stop();

    this->serial = NULL;
#else
	std::cout << "mcssl_finish(CSSL)\n";
#endif
}

void BaseControl::McsslCallback(int id, uint8_t* buf, int length)
{
    struct timeval now;
    gettimeofday(&now, 0);
//    printf("duration: %f %f\n", now.tv_usec, now.tv_sec);


    if((*(buf + 0) == 0xff) && (*(buf + 1) == 0xfa)){
        uint8_t data[RX_PACKAGE_SIZE] = {  *(buf + 0), *(buf + 1), *(buf + 2), *(buf + 3), *(buf + 4), 
                            *(buf + 5), *(buf + 6), *(buf + 7), *(buf + 8), *(buf +9)};
        Crc_16 Crc16(data, sizeof(data)/sizeof(uint8_t));
        unsigned short crc_16 = Crc16.getCrc(data, sizeof(data)/sizeof(uint8_t));
        if(crc_16 == 0){
            long duration_s = now.tv_sec - last_time.tv_sec;
            long duration_us = now.tv_usec - last_time.tv_usec;
            base_RX.id = id;
            base_RX.size = length;
            base_RX.duration = duration_s * 1000000 + duration_us;
            base_RX.w1 = (data[2] << 8) + data[3];
            base_RX.w2 = (data[4] << 8) + data[5];
            base_RX.w3 = (data[6] << 8) + data[7];
            base_flag = true;
            last_time = now;
        }else{
            base_flag = false;
        }
    }
#ifdef DEBUG_CSSLCALLBACK
    for(int i = 0; i < length; i++){
        printf("%x ",*(buf + i));
    }
    printf("\n");
#endif
}

void BaseControl::McsslSend2FPGA()
{	
    this->base_TX.w1_l = this->w1;
    this->base_TX.w1_h = this->w1 >> 8;
    this->base_TX.w2_l = this->w2;
    this->base_TX.w2_h = this->w2 >> 8;
    this->base_TX.w3_l = this->w3;
    this->base_TX.w3_h = this->w3 >> 8;
    this->base_TX.enable_and_stop = (this->en1<<7) + (this->en2<<6) + (this->en3<<5) + (this->stop1<<4) + (this->stop2<<3) + (this->stop3<<2) + (this->hold_ball);
    this->base_TX.shoot = this->shoot_power;
    uint8_t crc_data[TX_PACKAGE_SIZE - 2] = {
        this->base_TX.head1, 
        this->base_TX.head2, 
        this->base_TX.w1_h, 
        this->base_TX.w1_l, 
        this->base_TX.w2_h, 
        this->base_TX.w2_l, 
        this->base_TX.w3_h, 
        this->base_TX.w3_l, 
        this->base_TX.enable_and_stop, 
        this->base_TX.shoot
    };

//    Crc_16 Crc16(crc_data, TX_PACKAGE_SIZE - 2);
    crc_16 = Crc.getCrc(crc_data, TX_PACKAGE_SIZE -2);
//    this->base_TX.crc_16_1 = *(unsigned char*)(&crc_16) + 1;
//    this->base_TX.crc_16_2 = *(unsigned char*)(&crc_16) + 0;
    this->base_TX.crc_16_1 = crc_16 >> 8;
    this->base_TX.crc_16_2 = crc_16;
    uint8_t cssl_data[TX_PACKAGE_SIZE + 1] = {  
        this->base_TX.head1, 
        this->base_TX.head2, 
        this->base_TX.w1_h, 
        this->base_TX.w1_l, 
        this->base_TX.w2_h, 
        this->base_TX.w2_l, 
        this->base_TX.w3_h, 
        this->base_TX.w3_l, 
        this->base_TX.enable_and_stop, 
        this->base_TX.shoot,
        this->base_TX.crc_16_1,
        this->base_TX.crc_16_2,
        0
    };
#ifdef CSSL
    cssl_putdata(serial, cssl_data, TX_PACKAGE_SIZE + 1);
#ifdef DEBUG
    printf("**************************\n");
    printf("* mcssl_send(DEBUG) *\n");
    printf("**************************\n");
    printf("head1: %x\n", (this->base_TX.head1));
    printf("head2: %x\n", (this->base_TX.head2));
    printf("w1_h: %x\n", (this->base_TX.w1_h));
    printf("w1_l: %x\n", (this->base_TX.w1_l));
    printf("w2_h: %x\n", (this->base_TX.w2_h));
    printf("w2_l: %x\n", (this->base_TX.w2_l));
    printf("w3_h: %x\n", (this->base_TX.w3_h));
    printf("w3_l: %x\n", (this->base_TX.w3_l));
    printf("enable_and_stop: %x\n", (this->base_TX.enable_and_stop));
    printf("shoot: %x\n", (this->base_TX.shoot));
    printf("crc16-1: %x\n", (this->base_TX.crc_16_1));
    printf("crc16-2: %x\n", (this->base_TX.crc_16_2));
    printf("crc16: %x\n", (crc_16));
    printf("w1: %x\n", ((this->base_TX.w1_h) << 8) + (this->base_TX.w1_l));
    printf("w2: %x\n", ((this->base_TX.w2_h) << 8) + (this->base_TX.w2_l));
    printf("w3: %x\n", ((this->base_TX.w3_h) << 8) + (this->base_TX.w3_l));
#endif
#else
    printf("**************************\n");
    printf("* mcssl_send(CSSL) *\n");
    printf("**************************\n");
    printf("head1: %x\n", (this->base_TX.head1));
    printf("head2: %x\n", (this->base_TX.head2));
    printf("w1_h: %x\n", (this->base_TX.w1_h));
    printf("w1_l: %x\n", (this->base_TX.w1_l));
    printf("w2_h: %x\n", (this->base_TX.w2_h));
    printf("w2_l: %x\n", (this->base_TX.w2_l));
    printf("w3_h: %x\n", (this->base_TX.w3_h));
    printf("w3_l: %x\n", (this->base_TX.w3_l));
    printf("enable_and_stop: %x\n", (this->base_TX.enable_and_stop));
    printf("shoot: %x\n", (this->base_TX.shoot));
    printf("crc16-1: %x\n", (this->base_TX.crc_16_1));
    printf("crc16-2: %x\n", (this->base_TX.crc_16_2));
    printf("crc16: %x\n", (crc_16));
    printf("w1: %x\n", ((this->base_TX.w1_h) << 8) + (this->base_TX.w1_l));
    printf("w2: %x\n", ((this->base_TX.w2_h) << 8) + (this->base_TX.w2_l));
    printf("w3: %x\n", ((this->base_TX.w3_h) << 8) + (this->base_TX.w3_l));
#endif
}



void BaseControl::ForwardKinematics()
{
	double x,y;
	double yaw=0;
	int round=0;
	this->base_robotFB.x_speed = ( base_RX.w1 * (-0.3333) + base_RX.w2 * (-0.3333) + base_RX.w3 * (0.6667)) * 2 * M_PI * wheel_radius / 26 / 2000;
	this->base_robotFB.y_speed = ( base_RX.w1 * (0.5774) + base_RX.w2 * (-0.5774) + base_RX.w3 * (0)) * 2 * M_PI * wheel_radius / 26 / 2000;
	yaw = (base_RX.w1 * yaw_inv + base_RX.w2 * yaw_inv + base_RX.w3 * yaw_inv) * 2 * M_PI * wheel_radius / 2000 / 26;
	round = yaw/(2*M_PI);
	double yaw_degree;
	yaw_degree = (yaw - round*2*M_PI)*180/M_PI;
	if(yaw_degree>180){
		this->base_robotFB.yaw_speed = yaw_degree-360;
	}else if(yaw_degree<(-180)){
		this->base_robotFB.yaw_speed = yaw_degree+360;
	}else{
		this->base_robotFB.yaw_speed = yaw_degree;
	}
}

void BaseControl::InverseKinematics()
{
    /**********************************
      w1, w2, w3 speed are speed percent
     **********************************/
	double w1_speed, w2_speed, w3_speed;


	w1_speed = -this->x_CMD*cos(m1_Angle)+this->y_CMD*sin(m1_Angle)+this->yaw_CMD*robot_radius*(-1);
	w2_speed = -this->x_CMD*cos(m2_Angle)+this->y_CMD*sin(m2_Angle)+this->yaw_CMD*robot_radius*(-1);
	w3_speed = -this->x_CMD*cos(m3_Angle)+this->y_CMD*sin(m3_Angle)+this->yaw_CMD*robot_radius*(-1);
#ifdef DEBUG
	std::cout << "Inverse kinematics(DEBUG)\n";
	std::cout << std::dec;
	std::cout << "x_speed CMD: " << (this->x_CMD) << std::endl;
	std::cout << "y_speed CMD: " << (this->y_CMD) << std::endl;
	std::cout << "yaw_speed CMD: " << (this->yaw_CMD) << std::endl;
	std::cout << "w1_speed(%): " << w1_speed << std::endl;
	std::cout << "w2_speed(%): " << w2_speed << std::endl;
	std::cout << "w3_speed(%): " << w3_speed << std::endl;
	std::cout << std::endl;
#endif
	SpeedRegularization(w1_speed, w2_speed, w3_speed);
}

void BaseControl::SpeedRegularization(double w1_p, double w2_p, double w3_p)
{
//	unsigned char w1_dir = (w1>0)? 0x80 : 0;
//	unsigned char w2_dir = (w2>0)? 0x80 : 0;
//	unsigned char w3_dir = (w3>0)? 0x80 : 0;

	w1_p = (fabs(w1_p)<0.001)? 0 : w1_p;
	w2_p = (fabs(w2_p)<0.001)? 0 : w2_p;
	w3_p = (fabs(w3_p)<0.001)? 0 : w3_p;
	w1_p = (w1_p < -100)? -100 : (w1_p > 100)? 100 : w1_p;
	w2_p = (w2_p < -100)? -100 : (w2_p > 100)? 100 : w2_p;
	w3_p = (w3_p < -100)? -100 : (w3_p > 100)? 100 : w3_p;
//	if(w1_speed_percent >= 100)w1_speed_percent = 100;
//	if(w2_speed_percent >= 100)w2_speed_percent = 100;
//	if(w3_speed_percent >= 100)w3_speed_percent = 100;
//	enable
	this->en1 = (fabs(w1_p) > 0)? 1 : 0;
	this->en2 = (fabs(w2_p) > 0)? 1 : 0;
	this->en3 = (fabs(w3_p) > 0)? 1 : 0;
	this->stop1 = 0;
	this->stop2 = 0;
	this->stop3 = 0;
//	real motor speed(rpm)
    this->w1 = (int16_t)w1_p * MAX_MOTOR_RPM / 100;
    this->w2 = (int16_t)w2_p * MAX_MOTOR_RPM / 100;
    this->w3 = (int16_t)w3_p * MAX_MOTOR_RPM / 100;
//	this->base_TX.w1 = (w1_speed_percent>0)? (unsigned char)((127*0.85*w1_speed_percent/100) + 12.7 + w1_dir) : 0x80;
//	this->base_TX.w2 = (w2_speed_percent>0)? (unsigned char)((127*0.85*w2_speed_percent/100) + 12.7 + w2_dir) : 0x80;
//	this->base_TX.w3 = (w3_speed_percent>0)? (unsigned char)((127*0.85*w3_speed_percent/100) + 12.7 + w3_dir) : 0x80;
#ifdef DEBUG
	std::cout << "speed_regularization(DEBUG)\n";
	std::cout << std::hex;
	std::cout << "motor1 speed(hex): " << (this->w1) << std::endl;
	std::cout << "motor2 speed(hex): " << (this->w2) << std::endl;
	std::cout << "motor3 speed(hex): " << (this->w3) << std::endl;
	std::cout << std::hex;
	std::cout << "enable&stop(hex): "  << (this->base_TX.enable_and_stop) << std::endl;
	std::cout << std::endl;
#endif

}

void BaseControl::ShootRegularization()
{
	if(this->shoot_power == 0){
		this->shoot_power = 0;
	}else if(this->shoot_power >= 100){
		this->shoot_power = 255;
	}
	else{
		this->shoot_power = (255 * this->shoot_power / 100);
	}
#ifdef DEBUG
	std::cout << "shoot_regularization(DEBUG)\n";
	std::cout << std::hex;
	std::cout << "shoot byte(hex): " << (this->shoot_power) << std::endl;
	std::cout << std::endl;
#endif
}

void BaseControl::Run()
{
    printf("in the run\n");
    while(true){

    }
}

void* BaseControl::pThreadRun(void* p)
{
    ((BaseControl*)p)->Run();
    pthread_exit(NULL);
}

bool BaseControl::GetBaseFlag()
{
//    std::cout << base_flag << std::endl;
    return base_flag;
}

serial_rx* BaseControl::GetPack()
{
    base_flag = false;
    return &base_RX;
}

void BaseControl::Send(const robot_command &CMD)
{
#ifdef DEBUG
	std::cout << "BaseControl::send(DEBUG)\n";
#endif
//	this->base_robotCMD = CMD;
    this->x_CMD = CMD.x_speed;
	this->y_CMD = CMD.y_speed;
	this->yaw_CMD = CMD.yaw_speed;
    this->shoot_power = CMD.shoot_power;
    this->hold_ball = CMD.hold_ball;
    this->remote = CMD.remote;
	ShootRegularization();
	InverseKinematics();
	McsslSend2FPGA();
}

robot_command *BaseControl::GetFeedback()
{
	ForwardKinematics();
	return &base_robotFB;
}