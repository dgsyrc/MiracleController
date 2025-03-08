#include "chassis/chassis.h"
#define MAX_OUTPUT 4000
#define MAX_INPUT 660
#define SCALE_FACTOR (MAX_OUTPUT / (float)MAX_INPUT)

using namespace std;

int err_list[20], cnt = 0;

void calculate(Chassis *chassis)
{
    // 归一化并缩放输入信号
    double vx = chassis->controlerInfo.ch0 * SCALE_FACTOR;
    double vy = chassis->controlerInfo.ch1 * SCALE_FACTOR;
    double rot = chassis->controlerInfo.ch2 * SCALE_FACTOR;
    chassis->M3508_sendInfo[0].I_data = (int16_t)chassis->pid_controller[0].compute((vx - vy + rot) * 1.0, (chassis->M3508_receiveInfo[0].speed.speed_data) * 1.0);
    int maxn, minn, sum = 0;
    if (cnt < 20)
    {
        err_list[cnt] = (int)(vx - vy + rot) - (int)chassis->M3508_receiveInfo[0].speed.speed_data;
        cnt++;
        for (int i = 0; i < cnt; ++i)
        {
            maxn = (maxn < err_list[i]) ? err_list[i] : maxn;
            minn = (minn > err_list[i]) ? err_list[i] : minn;
            sum = sum + err_list[i];
        }
    }
    else
    {
        for (int i = 0; i < 19; ++i)
        {
            err_list[i] = err_list[i + 1];
        }
        err_list[19] = (int)(vx - vy + rot) - (int)chassis->M3508_receiveInfo[0].speed.speed_data;
        maxn = err_list[19], minn = err_list[19], sum = err_list[19];
        for (int i = 0; i < 19; ++i)
        {
            maxn = (maxn < err_list[i]) ? err_list[i] : maxn;
            minn = (minn > err_list[i]) ? err_list[i] : minn;
            sum = sum + err_list[i];
        }
    }
    printf("err:%4d maxn:%4d minn:%4d avg:%5.1lf\n", (int)(vx - vy + rot) - (int)chassis->M3508_receiveInfo[0].speed.speed_data, maxn, minn, sum * 1.0 / cnt);
    // 计算四个电机的控制量（麦轮公式）
    // chassis->M3508_sendInfo[0].I_data = vx - vy + rot;
    chassis->M3508_sendInfo[1].I_data = (int16_t)chassis->pid_controller[1].compute((vx + vy + rot) * 1.0, (chassis->M3508_receiveInfo[1].speed.speed_data) * 1.0);
    chassis->M3508_sendInfo[2].I_data = (int16_t)chassis->pid_controller[2].compute((-vx + vy + rot) * 1.0, (chassis->M3508_receiveInfo[2].speed.speed_data) * 1.0);
    chassis->M3508_sendInfo[3].I_data = (int16_t)chassis->pid_controller[3].compute((-vx - vy + rot) * 1.0, (chassis->M3508_receiveInfo[3].speed.speed_data) * 1.0);
}

void start(Chassis *chassis)
{
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;

    // 创建socket
    s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0)
    {
        perror("Error while opening socket");
        return;
    }

    // 设置CAN接口
    strcpy(ifr.ifr_name, "can0");
    ioctl(s, SIOCGIFINDEX, &ifr);

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    // 绑定socket
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("Error in socket bind");
        return;
    }

    struct can_frame frame;

    chassis->pid_controller[0].setPID(15.5, 0.03, 0.125, -4000.0, 4000.0);
    chassis->pid_controller[1].setPID(15.5, 0.03, 0.125, -4000.0, 4000.0);
    chassis->pid_controller[2].setPID(15.5, 0.03, 0.125, -4000.0, 4000.0);
    chassis->pid_controller[3].setPID(15.5, 0.03, 0.125, -4000.0, 4000.0);

    while (1)
    {
        calculate(chassis);

        // 发送CAN帧
        frame.can_id = 0x200;
        frame.can_dlc = 8;
        frame.data[0] = chassis->M3508_sendInfo[0].raw_data[1];
        frame.data[1] = chassis->M3508_sendInfo[0].raw_data[0];
        frame.data[2] = chassis->M3508_sendInfo[1].raw_data[1];
        frame.data[3] = chassis->M3508_sendInfo[1].raw_data[0];
        frame.data[4] = chassis->M3508_sendInfo[2].raw_data[1];
        frame.data[5] = chassis->M3508_sendInfo[2].raw_data[0];
        frame.data[6] = chassis->M3508_sendInfo[3].raw_data[1];
        frame.data[7] = chassis->M3508_sendInfo[3].raw_data[0];

        if (write(s, &frame, sizeof(frame)) != sizeof(frame))
        {
            perror("Error in sending frame");
            return;
        }

        frame.can_id = 0x1FF;
        frame.can_dlc = 8;
        frame.data[0] = 0x00;
        frame.data[1] = 0x00;
        frame.data[2] = 0x00;
        frame.data[3] = 0x00;
        frame.data[4] = 0x00;
        frame.data[5] = 0x00;
        frame.data[6] = 0x00;
        frame.data[7] = 0x00;

        // 读取CAN帧
        if (read(s, &frame, sizeof(frame)) > 0)
        {
            if ((frame.can_id >> 4) == 0x20)
            {
                int id = (frame.can_id & 0x00F);

                if (id > 8)
                {
                    continue;
                }
                id--;
                chassis->M3508_receiveInfo[id].angle.raw_data[0] = frame.data[1];
                chassis->M3508_receiveInfo[id].angle.raw_data[1] = frame.data[0];

                chassis->M3508_receiveInfo[id].speed.raw_data[0] = frame.data[3];
                chassis->M3508_receiveInfo[id].speed.raw_data[1] = frame.data[2];

                chassis->M3508_receiveInfo[id].current.raw_data[0] = frame.data[5];
                chassis->M3508_receiveInfo[id].current.raw_data[1] = frame.data[4];

                chassis->M3508_receiveInfo[id].temp.raw_data = frame.data[6];

                printf("M3058 [%d]: Angle=%d Speed=%d Current=%d Temp=%d\n", id + 1, chassis->M3508_receiveInfo[id].angle.angle_data, chassis->M3508_receiveInfo[id].speed.speed_data, chassis->M3508_receiveInfo[id].current.I_data, chassis->M3508_receiveInfo[id].temp.temp_data);
            }
        }
    }

    // 关闭socket
    close(s);
}