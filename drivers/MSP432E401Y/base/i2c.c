/*
  i2c.c - I2C bridge interface for Trinamic TMC2130 stepper drivers

  For Texas Instruments SimpleLink ARM processors/LaunchPads

  Part of GrblHAL

  Copyright (c) 2018 Terje Io

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "driver.h"

#ifdef USE_I2C

#include "i2c.h"

typedef enum {
    I2CState_Idle = 0,
    I2CState_SendNext,
    I2CState_SendLast,
    I2CState_SendRegisterAddress,
    I2CState_AwaitCompletion,
    I2CState_ReceiveNext,
    I2CState_ReceiveNextToLast,
    I2CState_ReceiveLast,
} i2c_state_t;

typedef struct {
    volatile i2c_state_t state;
    uint8_t addr;
    uint8_t count;
    uint8_t *data;
    bool getKeycode;
#if KEYPAD_ENABLE
    keycode_callback_ptr keycode_callback;
#endif
    uint8_t buffer[8];
} i2c_trans_t;

static i2c_trans_t i2c;

#define i2cIsBusy ((i2c.state != I2CState_Idle) || I2CMasterBusy(I2C0_BASE))

static void I2C_interrupt_handler (void);

void I2CInit (void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0);
    SysCtlPeripheralReset(SYSCTL_PERIPH_I2C0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);

    GPIOPinConfigure(GPIO_PB2_I2C0SCL);
    GPIOPinConfigure(GPIO_PB3_I2C0SDA);
    GPIOPadConfigSet(GPIO_PORTB_BASE, GPIO_PIN_3, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_OD);

    GPIOPinTypeI2CSCL(GPIO_PORTB_BASE, GPIO_PIN_2);
    GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_3);
#ifdef __MSP432E401Y__
    I2CMasterInitExpClk(I2C0_BASE, 120000000, false);
#else // TM4C1294
    I2CMasterInitExpClk(I2C0_BASE, 120000000, false);
#endif
    I2CIntRegister(I2C0_BASE, I2C_interrupt_handler);

    i2c.count = 0;
    i2c.state = I2CState_Idle;

    I2CMasterIntClear(I2C0_BASE);
    I2CMasterIntEnable(I2C0_BASE);
}

// get bytes (max 8), waits for result
static uint8_t* I2C_Receive(uint32_t i2cAddr, uint32_t bytes, bool block)
{
    while(i2cIsBusy);

    i2c.data  = i2c.buffer;
    i2c.count = bytes;
    i2c.state = bytes == 1 ? I2CState_ReceiveLast : (bytes == 2 ? I2CState_ReceiveNextToLast : I2CState_ReceiveNext);

    I2CMasterSlaveAddrSet(I2C0_BASE, i2cAddr, true);
    I2CMasterControl(I2C0_BASE,  bytes == 1 ? I2C_MASTER_CMD_SINGLE_RECEIVE : I2C_MASTER_CMD_BURST_RECEIVE_START);

    if(block)
        while(i2cIsBusy);

    return i2c.buffer;
}

static void I2C_Send (uint32_t i2cAddr, uint8_t bytes, bool block)
{
    i2c.count = bytes - 1;
    i2c.data  = i2c.buffer;
    i2c.state = bytes == 1 ? I2CState_AwaitCompletion : (bytes == 2 ? I2CState_SendLast : I2CState_SendNext);
    I2CMasterSlaveAddrSet(I2C0_BASE, i2cAddr, false);
    I2CMasterDataPut(I2C0_BASE, *i2c.data++);
    I2CMasterControl(I2C0_BASE, bytes == 1 ? I2C_MASTER_CMD_SINGLE_SEND : I2C_MASTER_CMD_BURST_SEND_START);

    if(block)
        while(i2cIsBusy);
}

static uint8_t *I2C_ReadRegister (uint32_t i2cAddr, uint8_t bytes, bool block)
{
    while(i2cIsBusy);

    i2c.count = bytes;
    i2c.data  = i2c.buffer;
    i2c.state = I2CState_SendRegisterAddress;
    i2c.addr = i2cAddr;
    I2CMasterSlaveAddrSet(I2C0_BASE, i2cAddr, false);
    I2CMasterDataPut(I2C0_BASE, *i2c.data);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_START);

    if(block)
        while(i2cIsBusy);

    return i2c.buffer;
}

#if KEYPAD_ENABLE

void I2C_GetKeycode (uint32_t i2cAddr, keycode_callback_ptr callback)
{
    while(i2cIsBusy);

    i2c.keycode_callback = callback;

    I2C_Receive(i2cAddr, 1, false);
}

#endif

#if TRINAMIC_ENABLE && TRINAMIC_I2C

static TMC2130_status_t I2C_TMC_ReadRegister (TMC2130_t *driver, TMC2130_datagram_t *reg)
{
    uint8_t *res, i2creg;
    TMC2130_status_t status = {0};

    if((i2creg = TMCI2C_GetMapAddress((uint8_t)(driver ? (uint32_t)driver->cs_pin : 0), reg->addr).value) == 0xFF)
        return status; // unsupported register

    while(i2cIsBusy);

    i2c.buffer[0] = i2creg;
    i2c.buffer[1] = 0;
    i2c.buffer[2] = 0;
    i2c.buffer[3] = 0;
    i2c.buffer[4] = 0;

    res = I2C_ReadRegister(I2C_ADR_I2CBRIDGE, 5, true);

    status.value = (uint8_t)*res++;
    reg->payload.value = ((uint8_t)*res++ << 24);
    reg->payload.value |= ((uint8_t)*res++ << 16);
    reg->payload.value |= ((uint8_t)*res++ << 8);
    reg->payload.value |= (uint8_t)*res++;

    return status;
}


static TMC2130_status_t I2C_TMC_WriteRegister (TMC2130_t *driver, TMC2130_datagram_t *reg)
{
    TMC2130_status_t status = {0};

    while(i2cIsBusy);

    reg->addr.write = 1;
    i2c.buffer[0] = TMCI2C_GetMapAddress((uint8_t)(driver ? (uint32_t)driver->cs_pin : 0), reg->addr).value;
    reg->addr.write = 0;

    if(i2c.buffer[0] == 0xFF)
        return status; // unsupported register

    i2c.buffer[1] = (reg->payload.value >> 24) & 0xFF;
    i2c.buffer[2] = (reg->payload.value >> 16) & 0xFF;
    i2c.buffer[3] = (reg->payload.value >> 8) & 0xFF;
    i2c.buffer[4] = reg->payload.value & 0xFF;

    I2C_Send(I2C_ADR_I2CBRIDGE, 5, true);

    return status;
}

void I2C_DriverInit (TMC_io_driver_t *driver)
{
    driver->WriteRegister = I2C_TMC_WriteRegister;
    driver->ReadRegister = I2C_TMC_ReadRegister;
}

#endif

static void I2C_interrupt_handler (void)
{
    // based on code from https://e2e.ti.com/support/microcontrollers/tiva_arm/f/908/t/169882

    I2CMasterIntClear(I2C0_BASE);

//    if(I2CMasterErr(I2C0_BASE) == I2C_MASTER_ERR_NONE)

    switch(i2c.state) {

        case I2CState_Idle:
            break;

        case I2CState_SendNext:
            I2CMasterDataPut(I2C0_BASE, *i2c.data++);
            if(--i2c.count == 1)
                i2c.state = I2CState_SendLast;

            I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_CONT);
            break;

        case I2CState_SendLast:
            I2CMasterDataPut(I2C0_BASE, *i2c.data);
            I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_FINISH);

            i2c.state = I2CState_AwaitCompletion;
            break;

        case I2CState_SendRegisterAddress:
            I2CMasterSlaveAddrSet(I2C0_BASE, i2c.addr, true);
            I2CMasterControl(I2C0_BASE,  i2c.count == 1 ? I2C_MASTER_CMD_SINGLE_RECEIVE : I2C_MASTER_CMD_BURST_RECEIVE_START);
            i2c.state = i2c.count == 1 ? I2CState_ReceiveLast : (i2c.count == 2 ? I2CState_ReceiveNextToLast : I2CState_ReceiveNext);
            break;

        case I2CState_AwaitCompletion:
            i2c.count = 0;
            i2c.state = I2CState_Idle;
            break;

        case I2CState_ReceiveNext:
            *i2c.data++ = I2CMasterDataGet(I2C0_BASE);
            I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_RECEIVE_CONT);

            if(--i2c.count == 2)
                i2c.state = I2CState_ReceiveNextToLast;
            break;

        case I2CState_ReceiveNextToLast:
            *i2c.data++ = I2CMasterDataGet(I2C0_BASE);
            I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_RECEIVE_FINISH);

            i2c.count--;
            i2c.state = I2CState_ReceiveLast;
            break;

        case I2CState_ReceiveLast:
            *i2c.data = I2CMasterDataGet(I2C0_BASE);
            i2c.count = 0;
            i2c.state = I2CState_Idle;
          #if KEYPAD_ENABLE
            if(i2c.keycode_callback) {
                  //  if(GPIOIntStatus(KEYINTR_PORT, KEYINTR_PIN) != 0) { // only add keycode when key is still pressed
                i2c.keycode_callback(*i2c.data);
                i2c.keycode_callback = NULL;
            }
          #endif
            break;
    }
}

#endif

