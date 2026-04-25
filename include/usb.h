// USB/EHCI 驱动头文件
// EHCI (Enhanced Host Controller Interface) + USB 核心 + 大容量存储

#ifndef USB_H
#define USB_H

#include "types.h"

// ============================================================================
// EHCI 寄存器定义 (MMIO)
// ============================================================================

// 能力寄存器 (相对于 BAR0)
#define EHCI_CAP_CAPLENGTH    0x00  // 能力寄存器长度 (8位)
#define EHCI_CAP_HCSPARAMS    0x04  // 结构参数
#define EHCI_CAP_HCCPARAMS    0x08  // 能力参数

// 操作寄存器 (相对于 BAR0 + CAPLENGTH)
#define EHCI_OP_USBCMD        0x00  // USB 命令
#define EHCI_OP_USBSTS        0x04  // USB 状态
#define EHCI_OP_USBINTR       0x08  // USB 中断使能
#define EHCI_OP_FRINDEX       0x0C  // 帧索引
#define EHCI_OP_CTRLDSSEG     0x10  // 4G 段选择器
#define EHCI_OP_PERIODICBASE  0x14  // 周期帧列表基地址
#define EHCI_OP_ASYNCLISTADDR 0x18  // 异步列表地址
#define EHCI_OP_CONFIGFLAG    0x40  // 配置标志
#define EHCI_OP_PORTSC(n)     (0x44 + 4*(n))  // 端口状态控制

// USBCMD 位定义
#define EHCI_CMD_RS           0x00000001  // Run/Stop
#define EHCI_CMD_HCRESET      0x00000002  // 主机控制器复位
#define EHCI_CMD_PSE          0x00000010  // 周期调度使能
#define EHCI_CMD_ASE          0x00000020  // 异步调度使能
#define EHCI_CMD_IAAD         0x00000040  // 中断异步推进门铃
#define EHCI_CMD_ITC_MASK     0x00FF0000  // 中断阈值控制

// USBSTS 位定义
#define EHCI_STS_USBINT       0x00000001  // USB 中断
#define EHCI_STS_USBERRINT    0x00000002  // USB 错误中断
#define EHCI_STS_PCD          0x00000004  // 端口变化检测
#define EHCI_STS_FLR          0x00000008  // 帧列表翻转
#define EHCI_STS_HSE          0x00000010  // 主机系统错误
#define EHCI_STS_IAA          0x00000020  // 异步推进中断
#define EHCI_STS_HCHALTED     0x00001000  // HC 已停止
#define EHCI_STS_RECLAMATION  0x00002000  // 回收
#define EHCI_STS_PSS          0x00004000  // 周期调度状态
#define EHCI_STS_ASS          0x00008000  // 异步调度状态

// PORTSC 位定义
#define EHCI_PORTSC_CCS       0x00000001  // 当前连接状态
#define EHCI_PORTSC_CSC       0x00000002  // 连接状态变化
#define EHCI_PORTSC_PE        0x00000004  // 端口使能
#define EHCI_PORTSC_PEC       0x00000008  // 端口使能变化
#define EHCI_PORTSC_OCA       0x00000010  // 过流激活
#define EHCI_PORTSC_OCC       0x00000020  // 过流变化
#define EHCI_PORTSC_FPR       0x00000040  // 强制端口恢复
#define EHCI_PORTSC_SUSPEND   0x00000080  // 挂起
#define EHCI_PORTSC_RESET     0x00000100  // 端口复位
#define EHCI_PORTSC_LS_MASK   0x00000C00  // 线路状态
#define EHCI_PORTSC_PP        0x00001000  // 端口电源
#define EHCI_PORTSC_OWNER     0x00002000  // 端口所有者 (伴随控制器)
#define EHCI_PORTSC_PIC_MASK  0x0000C000  // 端口指示灯控制

// CONFIGFLAG
#define EHCI_CF_CF            0x00000001  // 配置标志

// ============================================================================
// EHCI 数据结构 (DMA)
// ============================================================================

// 链接指针标志
#define EHCI_LP_TERMINATE     0x00000001
#define EHCI_LP_TYPE_ITD      (0 << 1)
#define EHCI_LP_TYPE_QH       (1 << 1)
#define EHCI_LP_TYPE_SITD     (2 << 1)
#define EHCI_LP_TYPE_FSTN     (3 << 1)

// 队列头 (QH) - 必须 32 字节对齐
// CRITICAL: Intel 6 Series EHCI is 64-bit capable (HCCPARAMS bit 0 = 1).
// The controller reads ext_bufptr[5] for upper 32 bits of buffer addresses.
// Without these fields, the controller reads adjacent memory as ext_bufptr,
// creating invalid 64-bit DMA addresses that cause Master Abort!
struct ehci_qh {
  uint hlp;           // 水平链接指针
  uint epchar;        // 端点特征
  uint epcap;         // 端点能力
  uint curqtd;        // 当前 qTD 指针
  uint nextqtd;       // 下一个 qTD 指针
  uint altnextqtd;    // 备选下一个 qTD 指针
  uint token;         // 令牌
  uint bufptr[5];     // 缓冲区指针页 [31:0]
  uint ext_bufptr[5]; // 扩展缓冲区指针 [63:32] (64位EHCI必需!)
  uint pad[3];        // 软件使用/填充
} __attribute__((aligned(32)));

// QH epchar 位定义
#define QH_EPCHAR_DEVADDR(a)    ((a) & 0x7F)
#define QH_EPCHAR_INACTIVE      (1 << 7)
#define QH_EPCHAR_ENDPT(e)      (((e) & 0xF) << 8)
#define QH_EPCHAR_EPS_FULL      (0 << 12)
#define QH_EPCHAR_EPS_LOW       (1 << 12)
#define QH_EPCHAR_EPS_HIGH      (2 << 12)
#define QH_EPCHAR_DTC           (1 << 14)
#define QH_EPCHAR_H             (1 << 15)
#define QH_EPCHAR_MPL(m)        (((m) & 0x7FF) << 16)
#define QH_EPCHAR_C             (1 << 27)
#define QH_EPCHAR_RL(r)         (((r) & 0xF) << 28)

// QH epcap 位定义
#define QH_EPCAP_SMASK(s)       ((s) & 0xFF)
#define QH_EPCAP_CMASK(c)       (((c) & 0xFF) << 8)
#define QH_EPCAP_HUBADDR(a)     (((a) & 0x7F) << 16)
#define QH_EPCAP_PORTNUM(p)     (((p) & 0x7F) << 23)
#define QH_EPCAP_MULT(m)        (((m) & 0x3) << 30)

// 传输描述符 (qTD) - 必须 32 字节对齐
// CRITICAL: Must include ext_bufptr[5] for 64-bit EHCI controllers!
// Without these, the controller reads the NEXT qTD's data as ext_bufptr,
// creating 64-bit addresses in the TB range that cause Master Abort.
struct ehci_qtd {
  uint nextqtd;       // 下一个 qTD 指针
  uint altnextqtd;    // 备选下一个 qTD 指针
  uint token;         // 令牌
  uint bufptr[5];     // 缓冲区指针页 [31:0]
  uint ext_bufptr[5]; // 扩展缓冲区指针 [63:32] (64位EHCI必需!)
  uint pad[3];        // 软件使用/填充
} __attribute__((aligned(32)));

// qTD token 位定义
#define QTD_TOKEN_STATUS_MASK   0x000000FF
#define QTD_TOKEN_ACTIVE        (1 << 7)
#define QTD_TOKEN_HALTED        (1 << 6)
#define QTD_TOKEN_BUFERR        (1 << 5)
#define QTD_TOKEN_BABBLE        (1 << 4)
#define QTD_TOKEN_XACTERR       (1 << 3)
#define QTD_TOKEN_MISSED        (1 << 2)
#define QTD_TOKEN_SPLITXSTATE   (1 << 1)
#define QTD_TOKEN_PERR          (1 << 0)
#define QTD_TOKEN_PID_OUT       (0 << 8)
#define QTD_TOKEN_PID_IN        (1 << 8)
#define QTD_TOKEN_PID_SETUP     (2 << 8)
#define QTD_TOKEN_CERR(c)       (((c) & 0x3) << 10)
#define QTD_TOKEN_IOC           (1 << 15)
#define QTD_TOKEN_TOTALBYTES(n) (((n) & 0x7FFF) << 16)
#define QTD_TOKEN_DT            (1 << 31)

// ============================================================================
// USB 标准定义
// ============================================================================

// USB 请求类型
#define USB_DIR_OUT             0x00
#define USB_DIR_IN              0x80
#define USB_TYPE_STANDARD       0x00
#define USB_TYPE_CLASS          0x20
#define USB_RECIP_DEVICE        0x00
#define USB_RECIP_INTERFACE     0x01
#define USB_RECIP_ENDPOINT      0x02
#define USB_RECIP_OTHER         0x03

// USB 标准请求
#define USB_REQ_GET_STATUS      0x00
#define USB_REQ_CLEAR_FEATURE   0x01
#define USB_REQ_SET_FEATURE     0x03
#define USB_REQ_SET_ADDRESS     0x05
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_REQ_SET_CONFIG      0x09

// USB 描述符类型
#define USB_DESC_DEVICE         0x01
#define USB_DESC_CONFIG         0x02
#define USB_DESC_STRING         0x03
#define USB_DESC_INTERFACE      0x04
#define USB_DESC_ENDPOINT       0x05
#define USB_DESC_HUB            0x29

// USB 设备类
#define USB_CLASS_HUB           0x09
#define USB_CLASS_MASS_STORAGE  0x08

// USB 集线器请求
#define HUB_REQ_GET_STATUS      0x00
#define HUB_REQ_CLEAR_FEATURE   0x01
#define HUB_REQ_SET_FEATURE     0x03
#define HUB_REQ_GET_DESCRIPTOR  0x06

// USB 集线器端口特征
#define HUB_PORT_CONNECTION     0
#define HUB_PORT_ENABLE         1
#define HUB_PORT_SUSPEND        2
#define HUB_PORT_OVER_CURRENT   3
#define HUB_PORT_RESET          4
#define HUB_PORT_POWER          8
#define HUB_PORT_LOW_SPEED      9
#define HUB_C_PORT_CONNECTION   16
#define HUB_C_PORT_ENABLE       17
#define HUB_C_PORT_SUSPEND      18
#define HUB_C_PORT_OVER_CURRENT 19
#define HUB_C_PORT_RESET        20

// USB 设置包
struct usb_setup {
  uchar bmRequestType;
  uchar bRequest;
  ushort wValue;
  ushort wIndex;
  ushort wLength;
} __attribute__((packed));

// USB 设备描述符
struct usb_dev_desc {
  uchar bLength;
  uchar bDescriptorType;
  ushort bcdUSB;
  uchar bDeviceClass;
  uchar bDeviceSubClass;
  uchar bDeviceProtocol;
  uchar bMaxPacketSize0;
  ushort idVendor;
  ushort idProduct;
  ushort bcdDevice;
  uchar iManufacturer;
  uchar iProduct;
  uchar iSerialNumber;
  uchar bNumConfigurations;
} __attribute__((packed));

// USB 配置描述符
struct usb_config_desc {
  uchar bLength;
  uchar bDescriptorType;
  ushort wTotalLength;
  uchar bNumInterfaces;
  uchar bConfigurationValue;
  uchar iConfiguration;
  uchar bmAttributes;
  uchar bMaxPower;
} __attribute__((packed));

// USB 接口描述符
struct usb_iface_desc {
  uchar bLength;
  uchar bDescriptorType;
  uchar bInterfaceNumber;
  uchar bAlternateSetting;
  uchar bNumEndpoints;
  uchar bInterfaceClass;
  uchar bInterfaceSubClass;
  uchar bInterfaceProtocol;
  uchar iInterface;
} __attribute__((packed));

// USB 端点描述符
struct usb_ep_desc {
  uchar bLength;
  uchar bDescriptorType;
  uchar bEndpointAddress;
  uchar bmAttributes;
  ushort wMaxPacketSize;
  uchar bInterval;
} __attribute__((packed));

// USB 集线器描述符
struct usb_hub_desc {
  uchar bLength;
  uchar bDescriptorType;
  uchar bNbrPorts;
  ushort wHubCharacteristics;
  uchar bPwrOn2PwrGood;
  uchar bHubContrCurrent;
  uchar data[8];
} __attribute__((packed));

// ============================================================================
// USB 大容量存储 (Bulk-Only Transport)
// ============================================================================

// CBW (命令块包装器) 签名
#define CBW_SIGNATURE           0x43425355

// CSW (命令状态包装器) 签名
#define CSW_SIGNATURE           0x53425355

struct usb_cbw {
  uint dCBWSignature;
  uint dCBWTag;
  uint dCBWDataTransferLength;
  uchar bmCBWFlags;
  uchar bCBWLUN;
  uchar bCBWCBLength;
  uchar CBWCB[16];
} __attribute__((packed));

struct usb_csw {
  uint dCSWSignature;
  uint dCSWTag;
  uint dCSWDataResidue;
  uchar bCSWStatus;
} __attribute__((packed));

// SCSI 命令
#define SCSI_TEST_UNIT_READY    0x00
#define SCSI_INQUIRY            0x12
#define SCSI_READ_CAPACITY      0x25
#define SCSI_READ_10            0x28
#define SCSI_WRITE_10           0x2A

#endif
