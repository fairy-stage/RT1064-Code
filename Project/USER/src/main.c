/*********************************************************************************************************************
 * COPYRIGHT NOTICE
 * Copyright (c) 2019,逐飞科技
 * All rights reserved.
 * 技术讨论QQ群：一群：179029047(已满)  二群：244861897
 *
 * 以下所有内容版权均属逐飞科技所有，未经允许不得用于商业用途，
 * 欢迎各位使用并传播本程序，修改内容时必须保留逐飞科技的版权声明。
 *
 * @file       		main
 * @company	   		成都逐飞科技有限公司
 * @author     		逐飞科技(QQ3184284598)
 * @version    		查看doc内version文件 版本说明
 * @Software 		IAR 8.3 or MDK 5.28
 * @Target core		NXP RT1064DVL6A
 * @Taobao   		https://seekfree.taobao.com/
 * @date       		2019-04-30
 ********************************************************************************************************************/


//整套推荐IO查看Projecct文件夹下的TXT文本


//打开新的工程或者工程移动了位置务必执行以下操作
//第一步 关闭上面所有打开的文件
//第二步 project  clean  等待下方进度条走完

//下载代码前请根据自己使用的下载器在工程里设置下载器为自己所使用的

#include "headfile.h"

#include "timer_pit.h"
#include "encoder.h"
#include "buzzer.h"
#include "button.h"
#include "motor.h"
#include "elec.h"
#include "openart_mini.h"
#include "smotor.h"

#include "debugger.h"
#include "imgproc.h"
#include "attitude_solution.h"

#include <stdio.h>

extern pid_param_t servo_pid;

rt_sem_t camera_sem;

debugger_image_t img0 = CREATE_DEBUGGER_IMAGE("raw", MT9V03X_CSI_W, MT9V03X_CSI_H, NULL);
image_t img_raw = DEF_IMAGE(NULL, MT9V03X_CSI_W, MT9V03X_CSI_H);

float thres_value = 130;
debugger_param_t p0 = CREATE_DEBUGGER_PARAM("thres", 0, 255, 1, &thres_value);

float delta_value = 13;
debugger_param_t p1 = CREATE_DEBUGGER_PARAM("delta", 0, 255, 1, &delta_value);


debugger_param_t p2 = CREATE_DEBUGGER_PARAM("kp", -100, 100, 0.01, &servo_pid.kp);

float car_width = 38;
debugger_param_t p3 = CREATE_DEBUGGER_PARAM("car_width", 0, 250, 1, &car_width);

float begin_y = 167;
debugger_param_t p4 = CREATE_DEBUGGER_PARAM("begin_y", 0, MT9V03X_CSI_H, 1, &begin_y);

float angle_meter = 0.20;
debugger_param_t p5 = CREATE_DEBUGGER_PARAM("angle_meter", 0, 0.4, 0.01, &angle_meter);

float pixel_per_meter = 102;
debugger_param_t p6 = CREATE_DEBUGGER_PARAM("pixel_per_meter", 0, MT9V03X_CSI_H, 1, &pixel_per_meter);

float fit_error = 1;
debugger_param_t p7 = CREATE_DEBUGGER_PARAM("fit_error", 0, 10, 0.1, &fit_error);

bool show_bin = false;
debugger_option_t opt0 = CREATE_DEBUGGER_OPTION("show_bin", &show_bin);

bool show_line = false;
debugger_option_t opt1 = CREATE_DEBUGGER_OPTION("show_line", &show_line);

bool show_approx = false;
debugger_option_t opt2 = CREATE_DEBUGGER_OPTION("show_approx", &show_approx);

// 原图左边线
AT_DTCM_SECTION_ALIGN(int pts1[MT9V03X_CSI_H][2], 8);
// 原图右边线
AT_DTCM_SECTION_ALIGN(int pts2[MT9V03X_CSI_H][2], 8);
// 去畸变+透视变换后左边线
AT_DTCM_SECTION_ALIGN(float pts1_inv[MT9V03X_CSI_H][2], 8);
// 去畸变+透视变换后右边线
AT_DTCM_SECTION_ALIGN(float pts2_inv[MT9V03X_CSI_H][2], 8);
// 去畸变+透视变换后左边线直线拟合端点
AT_DTCM_SECTION_ALIGN(float line1[MT9V03X_CSI_H][2], 8);
// 去畸变+透视变换后右边线直线拟合端点
AT_DTCM_SECTION_ALIGN(float line2[MT9V03X_CSI_H][2], 8);
// 去畸变+透视变换后左边线直线端点角度一阶、二阶变化率
float line1_angle_d1[MT9V03X_CSI_H], line1_angle_d2[MT9V03X_CSI_H];
// 去畸变+透视变换后右边线直线端点角度一阶、二阶变化率
float line2_angle_d1[MT9V03X_CSI_H], line2_angle_d2[MT9V03X_CSI_H];
// 去畸变+透视变换后左边线直线拟合后等间隔采样点
AT_DTCM_SECTION_ALIGN(float line_pts1[MT9V03X_CSI_H][2], 8);
// 去畸变+透视变换后右边线直线拟合后等间隔采样点
AT_DTCM_SECTION_ALIGN(float line_pts2[MT9V03X_CSI_H][2], 8);
// 去畸变+透视变换后边线间隔采样点直接变换所得出的中线
AT_DTCM_SECTION_ALIGN(float pts_road[MT9V03X_CSI_H][2], 8);

int line_pts1_num = 0;
int line_pts2_num = 0;

#define DEBUG_PIN   D4

void cross_dot(const float x[3], const float y[3], float out[3]){
    out[0] = x[1]*y[2]-x[2]*y[1];
    out[1] = x[2]*y[0]-x[0]*y[2];
    out[2] = x[0]*y[1]-x[1]*y[0];
}

extern int clip(int x, int low, int up);
extern float mapx[240][376];
extern float mapy[240][376];


#define ENCODER_PER_METER   (5800.)

#define ROAD_HALF_WIDTH  (0.225)

void track_leftline(float line[][2], int num, float tracked[][2]){
    for(int i=2; i<num-2; i++){
        float dx = line[i+2][0] - line[i-2][0];
        float dy = line[i+2][1] - line[i-2][1];
        float dn = sqrt(dx*dx+dy*dy);
        dx /= dn;
        dy /= dn;
        tracked[i][0] = line[i][0] - dy * pixel_per_meter * ROAD_HALF_WIDTH;
        tracked[i][1] = line[i][1] + dx * pixel_per_meter * ROAD_HALF_WIDTH;
    }
}

void track_rightline(float line[][2], int num, float tracked[][2]){
    for(int i=2; i<num-2; i++){
        float dx = line[i+2][0] - line[i-2][0];
        float dy = line[i+2][1] - line[i-2][1];
        float dn = sqrt(dx*dx+dy*dy);
        dx /= dn;
        dy /= dn;
        tracked[i][0] = line[i][0] + dy * pixel_per_meter * ROAD_HALF_WIDTH;
        tracked[i][1] = line[i][1] - dx * pixel_per_meter * ROAD_HALF_WIDTH;
    }
}

enum {
    NORMAL, TURN, CROSS, YROAD
} road_type1 = NORMAL, road_type2 = NORMAL;
float da1 = 0, da2 = 0;
float error, dx, dy;

enum {
    TRACK_LEFT, TRACK_RIGHT
} track_type = TRACK_RIGHT;

uint32_t circle_time = 0;
int64_t circle_encoder = 1ll<<63ll , cross_encoder =  1ll<<63ll ,yoard_encoder=  1ll<<63ll  ;

enum {
    CIRCLE_LEFT_BEGIN,  CIRCLE_LEFT_IN,  CIRCLE_LEFT_RUNNING,  CIRCLE_LEFT_OUT, CIRCLE_LEFT_END,
    CIRCLE_RIGHT_BEGIN, CIRCLE_RIGHT_IN, CIRCLE_RIGHT_RUNNING, CIRCLE_RIGHT_OUT, CIRCLE_RIGHT_END,
    CIRCLE_NONE
} circle_type = CIRCLE_NONE;


enum{
  CROSS_RUNNING, CROSS_BEGIN, CROSS_IN, CROSS_OUT,
  CROSS_NONE
} cross_type = CROSS_NONE;

enum{
  YOARD_NONE , YOARD_IN, YOARD_RUNNING,YOARD_OUT
} yoard_type = YOARD_NONE;

enum{
  NONE_LEFT_LINE , NONE_RIGHT_LINE,
  HAVE_LEFT_LINE , HAVE_RIGHT_LINE,
  HAVE_BOTH_LINE , NONE_BOTH_LINE
} line_type = HAVE_BOTH_LINE;

typedef struct {
    float c, s;
} ei_t;

void ei_from_float(ei_t *x, float a){
    x->c = cos(a);
    x->s = sin(a);
}

float ei_to_float(ei_t x){
    return atan2(x.s, x.c);
}

ei_t ei_sum(ei_t x, ei_t y){
    ei_t z;
    z.c = x.c*y.c - x.s*y.s;
    z.s = x.c*y.s + x.s*y.c;
    return z;
}

ei_t ei_minus(ei_t x, ei_t y){
    ei_t z;
    z.c = x.c*y.c + x.s*y.s;
    z.s = x.s*y.c - x.c*y.s;
    return z;
}

int anchor_num = 80, num1, num2;
float circle_begin_yaw = 0;
extern euler_param_t eulerAngle;
uint16_t  none_left_line = 0, none_right_line = 0;
uint16_t  have_left_line = 0, have_right_line = 0;
uint16_t  none_both_line;

uint16_t judge_num1= 0, judge_num2 = 0;
float angle;

int yoard_num = 0;
int yoard_judge = 0, cross_judge = 0, circle_judge = 0;

void get_line_pts(float line[][2], int num, int idx, float len, float pts[2]){
    float dx, dy, dn;
    if(len >= 0){
        for(; idx<num-1; idx++){
            dx = line[idx+1][0]-line[idx][0];
            dy = line[idx+1][1]-line[idx][1];
            dn = sqrt(dx*dx+dy*dy);
            if(len < dn) break;
            len -= dn;
        }
    }else{
        len = -len;
        for(; idx>0; idx--){
            dx = line[idx-1][0]-line[idx][0];
            dy = line[idx-1][1]-line[idx][1];
            dn = sqrt(dx*dx+dy*dy);
            if(len < dn) break;
            len -= dn;
        }
    }
    pts[0] = line[idx][0] + dx / dn * len;
    pts[1] = line[idx][1] + dy / dn * len;
}

void get_line_angle(float line[][2], float line_angle_d1[], float line_angle_d2[], int num, float m){
    line_angle_d1[0] = line_angle_d2[0] = 0;
    line_angle_d1[num-1] = line_angle_d2[num-1] = 0;
    for(int i=1; i<num-1; i++){
        float pts[4][2];
        get_line_pts(line, num, i, -2*m*pixel_per_meter, pts[0]);
        get_line_pts(line, num, i, -1*m*pixel_per_meter, pts[1]);
        get_line_pts(line, num, i, 1*m*pixel_per_meter, pts[2]);
        get_line_pts(line, num, i, 2*m*pixel_per_meter, pts[3]);
        // 计算角度变化量（虚数表示法）
        ei_t a[4];
        ei_from_float(a+0, atan2f(pts[1][1]-pts[0][1], pts[1][0]-pts[0][0]));
        ei_from_float(a+1, atan2f(line[i][1]-pts[1][1], line[i][0]-pts[1][0]));
        ei_from_float(a+2, atan2f(pts[2][1]-line[i][1], pts[2][0]-line[i][0]));
        ei_from_float(a+3, atan2f(pts[3][1]-pts[2][1], pts[3][0]-pts[2][0]));
        
        line_angle_d1[i] = ei_to_float(ei_minus(a[2], a[1])) / 3.1415 * 180;
        line_angle_d2[i] = ei_to_float(ei_minus(ei_minus(ei_minus(a[3], a[2]),ei_sum(a[2], a[1])),ei_minus(a[1], a[0])));
    }
}

int main(void)
{
	camera_sem = rt_sem_create("camera", 0, RT_IPC_FLAG_FIFO);

    mt9v03x_csi_init();
    icm20602_init_spi();
    
    encoder_init();
//    buzzer_init();
//    button_init();
    motor_init();
//    elec_init();
//    display_init();
//    openart_mini();
    smotor_init();
    timer_pit_init();
    seekfree_wireless_init();
    
    pit_init();
    pit_start(PIT_CH3);
    
    // 
    gpio_init(DEBUG_PIN, GPI, 0, GPIO_PIN_CONFIG);
    
    debugger_init();
    debugger_register_image(&img0);
    debugger_register_param(&p0);
    debugger_register_param(&p1);
    debugger_register_param(&p2);
    debugger_register_param(&p3);
    debugger_register_param(&p4);
    debugger_register_param(&p5);
    debugger_register_param(&p6);
    debugger_register_param(&p7);
    debugger_register_option(&opt0);
    debugger_register_option(&opt1);
    debugger_register_option(&opt2);
    
    uint32_t t1, t2;
    uint64_t current_encoder ;

    EnableGlobalIRQ(0);
    while (1)
    {
        //等待摄像头采集完毕
        rt_sem_take(camera_sem, RT_WAITING_FOREVER);
        img_raw.data = mt9v03x_csi_image[0];
        img0.buffer = mt9v03x_csi_image[0];
        //开始处理摄像头图像
        t1 = pit_get_us(PIT_CH3);
        if(show_bin) {
            threshold(&img_raw, &img_raw, thres_value) ;
            int pt0[2] = {img_raw.width/2-car_width, begin_y};
            int pt1[2] = {0, begin_y};
            draw_line(&img_raw, pt0, pt1, 0);
            pt0[0] = img_raw.width/2+car_width;
            pt1[0] = img_raw.width-1;
            draw_line(&img_raw, pt0, pt1, 0);
        } else {
            //thres_value = getOSTUThreshold(&img_raw, 100, 200);
            
            int x1=img_raw.width/2-car_width, y1=begin_y;
            num1=sizeof(pts1)/sizeof(pts1[0]);
            for(;x1>0; x1--) if(AT_IMAGE(&img_raw, x1, y1) < thres_value 
                             || ((int)AT_IMAGE(&img_raw, x1, y1) - (int)AT_IMAGE(&img_raw, x1-1, y1)) > delta_value * 2) break;
            if(AT_IMAGE(&img_raw, x1+1, y1) >= thres_value) findline_lefthand_with_thres(&img_raw, thres_value, delta_value, x1+1, y1, pts1, &num1);
            else num1 = 0;
            
            int x2=img_raw.width/2+car_width, y2=begin_y;
            num2=sizeof(pts2)/sizeof(pts2[0]);
            for(;x2<img_raw.width-1; x2++) if(AT_IMAGE(&img_raw, x2, y2) < thres_value
                             || ((int)AT_IMAGE(&img_raw, x2, y2) - (int)AT_IMAGE(&img_raw, x2+1, y2)) > delta_value * 2) break;
            if(AT_IMAGE(&img_raw, x2-1, y2) >= thres_value) findline_righthand_with_thres(&img_raw, thres_value, delta_value, x2-1, y2, pts2, &num2);
            else num2 = 0;
            
            // 透视变换
            for(int i=0; i<num1; i++) {
                pts1_inv[i][0] = mapx[pts1[i][1]][pts1[i][0]];
                pts1_inv[i][1] = mapy[pts1[i][1]][pts1[i][0]];
            }
            for(int i=0; i<num2; i++) {
                pts2_inv[i][0] = mapx[pts2[i][1]][pts2[i][0]];
                pts2_inv[i][1] = mapy[pts2[i][1]][pts2[i][0]];
            }
                        
            // 直线拟合（去噪）
            int line1_num=sizeof(line1)/sizeof(line1[0]);
            int line2_num=sizeof(line2)/sizeof(line2[0]);
            if(num1 > 10) approx_lines_f(pts1_inv, num1, fit_error, line1, &line1_num);
            else line1_num = 0;
            if(num2 > 10) approx_lines_f(pts2_inv, num2, fit_error, line2, &line2_num);
            else line2_num = 0;
            
            // 拐点角度变化量
            get_line_angle(line1, line1_angle_d1, line1_angle_d2, line1_num, angle_meter);
            get_line_angle(line2, line2_angle_d1, line2_angle_d2, line2_num, angle_meter);
            
            
            // 等距离采样直线点
            float len;
            line_pts1_num = 0;
            len = 0;
            for(int i=0; i<line1_num-1; i++){
                float dx = line1[i+1][0]-line1[i][0];
                float dy = line1[i+1][1]-line1[i][1];
                float dn = sqrt(dx*dx+dy*dy);
                dx /= dn;
                dy /= dn;
                for(;len<dn; len+=1){
                    line_pts1[line_pts1_num][0] = line1[i][0] + dx*len;
                    line_pts1[line_pts1_num][1] = line1[i][1] + dy*len;
                    if(++line_pts1_num >= sizeof(line_pts1)/sizeof(line_pts1[0])) goto line_pts1_end;
                }
                len -= dn;
            }
            line_pts1_end: 
            line_pts2_num = 0;
            len = 0;
            for(int i=0; i<line2_num-1; i++){
                float dx = line2[i+1][0]-line2[i][0];
                float dy = line2[i+1][1]-line2[i][1];
                float dn = sqrt(dx*dx+dy*dy);
                dx /= dn;
                dy /= dn;
                for(;len<dn; len+=1){
                    line_pts2[line_pts2_num][0] = line2[i][0] + dx*len;
                    line_pts2[line_pts2_num][1] = line2[i][1] + dy*len;
                    if(++line_pts2_num >= sizeof(line_pts2)/sizeof(line_pts2[0])) goto line_pts2_end;
                }
                len -= dn;
            }
            line_pts2_end: 
            ((void)0);
            
            // 计算局部角度变化
            
            
            //求角度
            float line1_dx1, line1_dy1, line1_len1, line1_dx2, line1_dy2, line1_len2;
            int line1_i = 0;

            int corner_x1 = 0,corner_y1 = 0,corner_x2 = 0,corner_y2 = 0;
            for(int i=0; i<num1-1; i++){
                float dx = line1[i][0]-line1[i+1][0];
                float dy = line1[i][1]-line1[i+1][1];
                float len = sqrt(dx*dx+dy*dy);
                if(len / pixel_per_meter < 0.05) continue;
                if(line1_i == 0){
                    line1_dx1 = dx;
                    line1_dy1 = dy;
                    line1_len1 = len;
                    line1_i = 1;
                }else{
                    line1_dx2 = dx;
                    line1_dy2 = dy;
                    line1_len2 = len;

                    //左转角
                    corner_x1 = line2[1][0] ;
                    corner_y1 = line2[1][1];
                    break;
                }
            }
            
            float line2_dx1, line2_dy1, line2_len1, line2_dx2, line2_dy2, line2_len2;

            int line2_i = 0;
            for(int i=0; i<num2-1; i++){
                float dx = line2[i][0]-line2[i+1][0];
                float dy = line2[i][1]-line2[i+1][1];
                float len = sqrt(dx*dx+dy*dy);
                if(len / pixel_per_meter < 0.04) continue;
                if(line2_i == 0){
                    line2_dx1 = dx;
                    line2_dy1 = dy;
                    line2_len1 = len;
                    line2_i = 1;
                }else{
                    line2_dx2 = dx;
                    line2_dy2 = dy;
                    line2_len2 = len;

                    //右转角
                    corner_x2 = line2[i][0] ;
                    corner_y2 = line2[i][1];
                    break;
                }

            }
            
            da1 = 0;
            da2 = 0;
            if(line1_i == 1){
                da1 = acos((line1_dx1 * line1_dx2 + line1_dy1 * line1_dy2) / line1_len1 / line1_len2) * 180. / 3.1415;
            }
            if(line2_i == 1){
                da2 = acos((line2_dx1 * line2_dx2 + line2_dy1 * line2_dy2) / line2_len1 / line2_len2) * 180. / 3.1415;
            }
            
             //由角度判定线类型
            if(line1_len1 / pixel_per_meter > 0.5) road_type1 = NORMAL;
            else if(10 < da1 && da1 < 45) road_type1 = TURN;
            else if(50 < da1 && da1 < 70) road_type1 = YROAD;
            else if(75 < da1 && da1 < 120) road_type1 = CROSS;
            else road_type1 = NORMAL;
            
            if(line2_len1 / pixel_per_meter > 0.5) road_type2 = NORMAL;
            else if(10 < da2 && da2 < 45) road_type2 = TURN;
            else if(50 < da2 && da2 < 70) road_type2 = YROAD;
            else if(75 < da2 && da2 < 120) road_type2 = CROSS;
            else road_type2 = NORMAL;
            
            current_encoder = (motor_l.total_encoder + motor_r.total_encoder) / 2;
            
            //由线的类型判定道路类型
            if(road_type1 == YROAD && road_type2 == YROAD && yoard_type==YOARD_NONE){

                yoard_judge++;
                // 三叉
                if(yoard_judge>3)
                {
                  yoard_type = YOARD_IN;
                  yoard_encoder = current_encoder;
                }
                
            } else if(road_type1 == CROSS && road_type2 == NORMAL && circle_type==CIRCLE_NONE){
                // 左环
                circle_judge++;
                if(current_encoder - circle_encoder > ENCODER_PER_METER * 4 && circle_judge>3){
                    circle_encoder = current_encoder;
                    circle_type = CIRCLE_LEFT_BEGIN;
                }
            } else if(road_type1 == NORMAL && road_type2 == CROSS && circle_type==CIRCLE_NONE){
               //右环
                circle_judge++;
                if(current_encoder - circle_encoder > ENCODER_PER_METER * 4 && circle_judge>3){
                    circle_encoder = current_encoder;
                    circle_type = CIRCLE_RIGHT_BEGIN;
                }
            } else if(road_type1 == CROSS && road_type2 == CROSS && cross_type ==CROSS_NONE){
               // 十字
                cross_judge++;
              if(cross_judge>3)
              {
                  cross_type = CROSS_BEGIN;
              }
            }
            else
            {
              yoard_judge = 0;
              circle_judge = 0;
              cross_judge = 0;
            }

            //丢线判据，滤高频噪声
            judge_num1 = judge_num1 * 0.7 + num1 * 0.3;
            judge_num2 = judge_num2 * 0.7 + num2 * 0.3;


            anchor_num = 120 - 70 -15;

            //十字逻辑
            if(cross_type !=CROSS_NONE){
               //检测到十字，先按照近线走
               if(cross_type == CROSS_BEGIN)
               {
                 anchor_num = 15;
                  //近角点过少，进入远线控制
                  if(corner_y1 > MT9V03X_CSI_H -30 || corner_y2 >MT9V03X_CSI_H -30)
                  {
                     cross_type = CROSS_IN;
                     cross_encoder = current_encoder;
                  }
               }
               //远线控制进十字,begin_y渐变靠近防丢线
               else if(cross_type == CROSS_IN)
               {
                   /*
                   open_loop = 1;
                    //先丢线，后找到线
                    */
                    anchor_num = 15;
                    float dis = (current_encoder - cross_encoder) / (ENCODER_PER_METER *6/10);
                    begin_y = (int) (dis * (150 - 50) + 50);
                    car_width = 10;
                     //编码器打表过空白期
                    if(current_encoder - cross_encoder > ENCODER_PER_METER* 6/10  ||  (judge_num1<100 && judge_num2<100))
                    {
                      cross_type = CROSS_RUNNING;
                    }
               }
               //常规巡线，切回找线
               else if(cross_type == CROSS_RUNNING)
               {
                  begin_y = 167;
                  car_width = 38;
                  anchor_num = 15;

                  if(pts1_inv[num1-1][0] > MT9V03X_CSI_W/2 ||  num2 < anchor_num + 20)  track_type = TRACK_LEFT;
                  else if(pts2_inv[num2-1][0] < MT9V03X_CSI_W/2 ||  num1 < anchor_num + 20)  track_type = TRACK_RIGHT;
                  else track_type = TRACK_RIGHT;

                  //识别到一个角点，且拐点较近
                  if((road_type1 == CROSS && corner_y1 > MT9V03X_CSI_H -25) || (road_type2 == CROSS  && corner_y2 >MT9V03X_CSI_H -25))
                  {
                     cross_type = CROSS_OUT;
                     cross_encoder = current_encoder ;
                  }
               }
              //寻远线，编码器
               else if(cross_type == CROSS_OUT)
               {
                   anchor_num = 15;
                   float dis = (current_encoder - cross_encoder) / (ENCODER_PER_METER *6/10);
                   begin_y = (int) (dis * (150 - 50) + 50);
                   car_width = 0;
                   if(current_encoder - cross_encoder > ENCODER_PER_METER* 6/10)
                   {
                      cross_type = CROSS_NONE;
                   }
               }
            
            }
            //圆环逻辑
            else if(circle_type != CIRCLE_NONE){
                // 左环开始，寻外直道右线
                if(circle_type == CIRCLE_LEFT_BEGIN){
                    track_type = TRACK_RIGHT;

                    //先丢线后有线
                    if(judge_num1 < 5)  { none_left_line++;}
                    if(judge_num1 > 200 && none_left_line > 5)
                    {
                      have_left_line ++ ;
                      if(have_left_line > 5)
                      {
                        circle_type = CIRCLE_LEFT_IN;
                        none_left_line = 0;
                        have_left_line = 0;
                        circle_encoder = current_encoder;
                      }
                    }
                }
                 //入环，寻内圆左线
                else if(circle_type == CIRCLE_LEFT_IN){
                    track_type = TRACK_LEFT;
                    anchor_num = 30;

                    //编码器打表过1/4圆   应修正为右线为转弯无拐点
                    if(judge_num1< 50 || current_encoder - circle_encoder >= ENCODER_PER_METER * (3.14 *  1/5))
                    {circle_type = CIRCLE_LEFT_RUNNING;}
                }
                //正常巡线，寻外圆右线
                else if(circle_type == CIRCLE_LEFT_RUNNING){
                    track_type = TRACK_RIGHT;
                    anchor_num = 50;
                    //外环拐点
                    if(55 < da2 && da2 < 125)
                    {
                       circle_type = CIRCLE_LEFT_OUT;
                    }
                }
                //出环，寻内圆
                else if(circle_type == CIRCLE_LEFT_OUT){
                    track_type = TRACK_LEFT;
                    //右线长度加倾斜角度  应修正为右线找到且为直线
                    if(judge_num2 >150 && da2 <45)  {have_right_line++;}
                    if(have_right_line>10)
                    { circle_type = CIRCLE_LEFT_END;}
                }
                //走过圆环，寻右线
                else if(circle_type == CIRCLE_LEFT_END){

                    track_type = TRACK_RIGHT;
                     //左线先丢后有
                    if(judge_num1 < 50)  { none_left_line++;}
                    if(judge_num1 > 90 && none_left_line > 5)
                    { circle_type = CIRCLE_NONE;
                      none_left_line = 0;}
                }
                //右环控制，前期寻左直道
                else if(circle_type == CIRCLE_RIGHT_BEGIN){
                    track_type = TRACK_LEFT;

                    //先丢线后有线
                    if(judge_num2 < 5)  { none_right_line++;}
                    if(judge_num2 > 100 && none_right_line > 5)
                    {
                      have_right_line ++ ;
                      if(have_right_line > 5)
                      {
                         circle_type = CIRCLE_RIGHT_IN;
                         none_right_line = 0;
                         have_right_line = 0;
                         circle_encoder = current_encoder;
                      }
                    }
                }
                //入右环，寻右内圆环
                else if(circle_type == CIRCLE_RIGHT_IN){
                    track_type = TRACK_RIGHT;

                    anchor_num = 30;

                    //编码器打表过1/4圆   应修正为左线为转弯无拐点
                    if(judge_num2< 50 || current_encoder - circle_encoder >= ENCODER_PER_METER * (3.14 *  1/5))
                    {circle_type = CIRCLE_RIGHT_RUNNING;}

                }
               //正常巡线，寻外圆左线
                else if(circle_type == CIRCLE_RIGHT_RUNNING){
                    track_type = TRACK_LEFT;
                    anchor_num = 50;
                    //外环存在拐点,可再加拐点距离判据
                    if(55 < da1 && da1 < 125)
                    {
                       circle_type = CIRCLE_RIGHT_OUT;
                    }
                }
              //出环，寻内圆
                else if(circle_type == CIRCLE_RIGHT_OUT){
                    track_type = TRACK_RIGHT;
                    //左长度加倾斜角度  应修正左右线找到且为直线
                    if(judge_num2 >150 && da2 <45)  {have_right_line++;}
                    if(have_right_line>10)
                    { circle_type = CIRCLE_RIGHT_END;}
                }
                //走过圆环，寻左线
                else if(circle_type == CIRCLE_RIGHT_END){

                    track_type = TRACK_LEFT;
                    //左线先丢后有
                    if(judge_num2 < 50)  { none_right_line++;}
                    if(judge_num2 > 90 && none_right_line > 5)
                    { circle_type = CIRCLE_NONE;
                      none_right_line = 0;}
                }
            }

            //三叉逻辑
            else if(yoard_type != YOARD_NONE)
            {
                //两圈寻不同线
              track_type = (yoard_num%2 == 0) ? TRACK_LEFT : TRACK_RIGHT;

               //入三叉，防重复触发，编码器判据
               if(yoard_type == YOARD_IN)
               {
                if(current_encoder - yoard_encoder > ENCODER_PER_METER )
                {
                  yoard_type = YOARD_RUNNING;
                }
              }
              //常规巡线
               else if(yoard_type == YOARD_RUNNING)
               {
                //两边存在一个拐点  应修正为两拐点
                  if((road_type1 == CROSS) || (road_type2 == CROSS))
                  {
                     yoard_encoder = current_encoder;
                     track_type = YOARD_OUT;
                  }
              }
              //出三叉,防误触
              else
              {
                 if(current_encoder - yoard_encoder > ENCODER_PER_METER/2)
                 {
                    track_type = YOARD_NONE;
                    yoard_num ++;
                 }

              }

            }
            else {
                //最远点中界判定
                if(pts1_inv[num1-1][0] > MT9V03X_CSI_W/2 ||  num2 < anchor_num + 20)  track_type = TRACK_LEFT;
                else if(pts2_inv[num2-1][0] < MT9V03X_CSI_W/2 ||  num1 < anchor_num + 20)  track_type = TRACK_RIGHT;
                else track_type = TRACK_LEFT;
            }
            
            int track_num;
            if(track_type == TRACK_LEFT){
                track_leftline(line_pts1, line_pts1_num, pts_road);
                track_num = line_pts1_num;
            }else{
                track_rightline(line_pts2, line_pts2_num, pts_road);
                track_num = line_pts2_num;
            }
            
                       
            //根据图像计算出车模与赛道之间的位置偏差
            int idx = track_num < anchor_num + 3 ? track_num - 3 : anchor_num;
            dx = (pts_road[idx][0] - img_raw.width / 2.) / pixel_per_meter;
            dy = (img_raw.height - pts_road[idx][1]) / pixel_per_meter;
            error = -atan2f(dx, dy);
            
            //根据偏差进行PD计算
            angle = pid_solve(&servo_pid, error);// * 0.6 + angle * 0.4;
            angle = MINMAX(angle, -13, 13);

            //PD计算之后的值用于寻迹舵机的控制
            smotor1_control(servo_duty(SMOTOR1_CENTER + angle));
            
            // draw
            if(show_line){
                clear_image(&img_raw);
//                for(int i=0; i<num1; i++){
//                    AT_IMAGE(&img_raw, clip(pts1_inv[i][0], 0, img_raw.width-1), clip(pts1_inv[i][1], 0, img_raw.height-1)) = 255;
//                }
//                for(int i=0; i<num2; i++){
//                    AT_IMAGE(&img_raw, clip(pts2_inv[i][0], 0, img_raw.width-1), clip(pts2_inv[i][1], 0, img_raw.height-1)) = 255;
//                }
                for(int i=0; i<line_pts1_num; i++){
                    AT_IMAGE(&img_raw, clip(line_pts1[i][0], 0, img_raw.width-1), clip(line_pts1[i][1], 0, img_raw.height-1)) = 255;
                }
                for(int i=0; i<line_pts2_num; i++){
                    AT_IMAGE(&img_raw, clip(line_pts2[i][0], 0, img_raw.width-1), clip(line_pts2[i][1], 0, img_raw.height-1)) = 255;
                }
                for(int i=2; i<track_num-2; i++){
                    AT_IMAGE(&img_raw, clip(pts_road[i][0], 0, img_raw.width-1), clip(pts_road[i][1], 0, img_raw.height-1)) = 255;
                }
                for(int y=0; y<img_raw.height; y++){
                    AT_IMAGE(&img_raw, img_raw.width/2, y) = 255;
                }
                
                for(int i=-3; i<=3; i++){
                    AT_IMAGE(&img_raw, (int)pts_road[idx][0]+i, (int)pts_road[idx][1]) = 255;
                }
                for(int i=-3; i<=3; i++){
                    AT_IMAGE(&img_raw, (int)pts_road[idx][0], (int)pts_road[idx][1]+1) = 255;
                }


            }else if(show_approx){
                clear_image(&img_raw);
                int pt0[2], pt1[2];
                for(int i=1; i<line1_num; i++){
                    pt0[0] = line1[i-1][0];
                    pt0[1] = line1[i-1][1];
                    pt1[0] = line1[i][0];
                    pt1[1] = line1[i][1];
                    draw_line(&img_raw, pt0, pt1, 255);
                    if(fabs(line1_angle_d1[i]) > 45 && fabs(line1_angle_d2[i]) < 15){
                        for(int dx=-3; dx<=3; dx++){
                            AT_IMAGE(&img_raw, clip(line1[i][0]+dx, 0, img_raw.width-1), clip(line1[i][1], 0, img_raw.height-1)) = 255;
                        }
                        for(int dy=-3; dy<=3; dy++){
                            AT_IMAGE(&img_raw, clip(line1[i][0], 0, img_raw.width-1), clip(line1[i][1]+dy, 0, img_raw.height-1)) = 255;
                        }
                    }
                }
                for(int i=1; i<line2_num; i++){
                    pt0[0] = line2[i-1][0];
                    pt0[1] = line2[i-1][1];
                    pt1[0] = line2[i][0];
                    pt1[1] = line2[i][1];
                    draw_line(&img_raw, pt0, pt1, 255);
                    if(fabs(line2_angle_d1[i]) > 45 && fabs(line2_angle_d2[i]) < 15){
                        for(int dx=-3; dx<=3; dx++){
                            AT_IMAGE(&img_raw, clip(line2[i][0]+dx, 0, img_raw.width-1), clip(line2[i][1], 0, img_raw.height-1)) = 255;
                        }
                        for(int dy=-3; dy<=3; dy++){
                            AT_IMAGE(&img_raw, clip(line2[i][0], 0, img_raw.width-1), clip(line2[i][1]+dy, 0, img_raw.height-1)) = 255;
                        }
                    }
                }
            }
        }
        
        
        // print debug information
        uint32_t tmp = pit_get_us(PIT_CH3);
        static uint8_t buffer[64];
        int len = snprintf((char*)buffer, sizeof(buffer), "process=%dus, period=%dus, road1=%d, road2=%d\r\n", 
                            tmp-t1, tmp-t2, road_type1, road_type2);
        t2 = tmp;
        
        if(gpio_get(DEBUG_PIN)) {
            static int cnt = 0;
            if(++cnt % 5 == 0) debugger_worker();
        }
        else usb_cdc_send_buff(buffer, len);


    }
}

  



