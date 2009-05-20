/**@file ipcam_packet.h
 * Contains definitions of all system command packet types.
 * @warning: Note that all fields of 16 bits or greater are to be sent in standard network byte order.
 */

#ifndef _IPCAM_PACKET_H_
#define _IPCAM_PACKET_H_

#include "build.h"

#ifdef DEVICE_BUILD
#warning Building for DEVICE

#include "ipcam.h"
#include "ipcam_network.h"
#include "ipcam_flash.h"

//Make sure that LIBRARY_BUILD isn't also defined (only one build type should be defined)
#ifdef LIBRARY_BUILD
#error Multiple builds defined
#endif

#endif //DEVICE_BUILD


#ifdef LIBRARY_BUILD
//#warning Building for LIBRARY

#define FLASH_MAX_PAGENO 4095
#define FLASH_PAGE_SIZE 528

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Simple typedefs for basic network types. These are defined in ipcam_network.h on the camera.
typedef uint32_t IPAddress;
typedef uint8_t MACAddress[6];
typedef uint16_t UDPPort;

typedef struct {
    MACAddress mac;
    IPAddress addr;
    UDPPort port;
} NetHost;

//Make sure that DEVICE_BUILD isn't also defined (only one build type should be defined)
#ifdef DEVICE_BUILD
#error Multiple builds defined
#endif

#endif //LIBRARY_BUILD

/**
 * All non-video packets will include this pre-defined magic number to identify them.
 */
#define WG_MAGIC_NO 0x00DEAF42UL

/**
 * All cameras will listen on this hard-coded port for commands
 *
 * @todo Ask WG if they have a port number preference. This one is just a placeholder.
 */
#define WG_CAMCMD_PORT 1627


/**
 * @defgroup PacketTypes    Complete listing of valid command packet types.
 * Packet types starting with zero and going up to PKT_MAX_ID are packets to be received by the camera.
 * Packet types from 0x80 and up are packets to be generated by the camera
 */
/*@{*/
#define PKTT_DISCOVER   0
#define PKTT_CONFIGURE  1
#define PKTT_VIDSTART   2
#define PKTT_VIDSTOP    3
#define PKTT_RESET      4
#define PKTT_TIMEREQ    5
#define PKTT_FLASHREAD  6
#define PKTT_FLASHWRITE 7
#define PKTT_TRIGCTRL   8
#define PKTT_SENSORRD   9
#define PKTT_SENSORWR   10
#define PKTT_SENSORSEL  11
#define PKTT_IMGRMODE   12
#define PKTT_IMGRSETRES 13
#define PKTT_SYSCONFIG  14
#define PKT_MAX_ID PKTT_SYSCONFIG

#define PKTT_ANNOUNCE   0x80
#define PKTT_TIMEREPLY  0x81
#define PKTT_STATUS     0x82
#define PKTT_FLASHDATA  0x83
#define PKTT_SENSORDATA 0x84
/*@}*/

/**
 * A PacketGeneric contains only the basic elements that are included in all camera command packets.
 * In conjunction with GenericFrame it is used for pre-validating packets.
 * Functions that generate packets with no data other than the 'type' field may also use a PacketGeneric.
 */
typedef struct PACKED_ATTRIBUTE {
    uint32_t magic_no;      ///< The Willow Garage Magic number (always WG_MAGIC_NO)
    uint32_t type;          ///< The packet type (see list of packet types, above)
    char hrt[16];           ///< A human-readable text field describing the packet contents
    NetHost reply_to;       ///< All packet replies should be directed to this host
} PacketGeneric;

#ifdef DEVICE_BUILD  // The host uses the Linux network stack so doesn't require this section
/**
 * The NetHeader structure encapsulates all of the generic network headers required for all UDP packets
 * that are sent over the EMAC32 peripheral. This includes the EMAC-specifc headers as well as the IP and UDP
 * header elements.
 */
typedef struct PACKED_ATTRIBUTE {
    EMACHeader emacHdr;
    IPHeader ipHdr;
    UDPHeader udpHdr;
} NetHeader;


/**
 * A GenericFrame describes the full headers of one network command (not video) frame used by the EMAC32.
 * The size of a GenericFrame is the minimum valid size of a command frame and is used for pre-filling and
 * pre-validating packet header information before the full size is known.
 *
 * Since the EMAC32 adds additional wrapping to the packet (preamble, CRC, etc) the size of the packet on the
 * wire will be slightly different from the size of the packet in the EMAC buffer.
 */
typedef struct PACKED_ATTRIBUTE {
    NetHeader hdr;
    PacketGeneric gPkt;
} GenericFrame;
#endif //DEVICE_BUILD

/**
 * A PacketDiscover is sent from the host in order to detect cameras on the network segment
 * and receive information about them.
 * It can be directed to the broadcast address (MAC ff:ff:ff:ff:ff:ff) to detect all cameras
 * or to the unicast address of one camera to detect only that specific camera.
 *
 * The PacketDiscover is valid in all camera modes. It consists of a PacketGeneri
 * with the type field set to PKTT_DISCOVER and an IP Address.
 * When received, the camera will reply with a PacketAnnounce using the specified IP Address
 *
 * @see PacketAnnounce
 */
typedef struct PACKED_ATTRIBUTE {
    /// Generic Command Packet Headers
    PacketGeneric hdr;

    /// IP Address device should use when responding
    IPAddress ip_addr;

} PacketDiscover;

/**
 * A PacketConfigure is generated by the host to configure the session IP address of a specific camera.
 * After a reset, all camera will boot with the same default IP address; to prevent address conflicts,
 * all cameras must be Configured before any further actions are taken.
 *
 * Valid States:
 * The PacketConfigure is valid in the Unconfigured and Configured states. It may not be sent while the
 * camera is streaming video.
 *
 * Addressing:
 * This packet should normally be directed to the broadcast address to ensure that the desired camera
 * receives it regardless of the state of the host system's ARP cache. If the product_id and ser_no fields
 * do not match those stored in the camera, then the camera will drop it.
 *
 * Response:
 * Once a camera has received a PacketConfigure and set its IP address, it will generate an PacketAnnounce
 * from the new address so that the remote host can verify it was correctly set.
 *
 * @todo Review specified behavior when a camera is reconfigured
 */
typedef struct PACKED_ATTRIBUTE {
    /// Generic Command Packet Headers
    PacketGeneric hdr;

    /// Camera Identification Section
    uint32_t product_id;        ///< Always CONFIG_PRODUCT_ID (6805018) for this product
    uint32_t ser_no;            ///< Indicates the specific serial number of this unit from the flash

    /// Configuration Section
    uint32_t ip_addr;           ///< The unique session IP address for the camera to use

} PacketConfigure;


/**
 * A PacketVidStart is generated by the host to instruct a camera to begin streaming video in the
 * currently configured video mode. The 'receiver' field instructs the camera where to send the video.
 *
 * Valid States:
 * The PacketVidStart is only valid in the Configured state. Unconfigured cameras and cameras currently
 * streaming will not respond to this packet.
 *
 * Addressing:
 * This packet may only be addressed to the unicast address of a single camera.
 *
 * Response:
 * Before starting the video stream, the camera will reply to the originator of the PacketVidStart with
 * a Status packet indicating success.
 */
typedef struct PACKED_ATTRIBUTE {
    /// Generic Command Packet Headers
    PacketGeneric hdr;

    /// Receiver Designation
    NetHost receiver;           ///< The Ethernet MAC, IP address, and UDP port to which to send video
} PacketVidStart;

/**
 * A PacketVidStop is generated by the host to terminate video streaming from a camera. After the camera receives
 * the PacketVidStop it will wait for the current frame to complete, then end transmission and revert to Configured mode.
 * The PacketVidStop is a PacketGeneric with the type field set to PKTT_VIDSTOP.
 *
 * Valid States:
 * A PacketVidStop is only valid in the Video state.
 *
 * Addressing:
 * This packet may only be sent to the unicast address of a single camera.
 *
 * Response:
 * After the current frame has completed, the camera will reply to the originator of the PacketVidStop with
 * a Status packet indicating success.
 */
typedef struct PACKED_ATTRIBUTE { PacketGeneric hdr; } PacketVidStop;

/**
 * A PacketReset is generated by the host to immediately return the camera to its power-up default state. All operations are
 * terminated and all temporary state is reset. The PacketReset is a PacketGeneric with the type field set to PKTT_RESET.
 *
 * Valid States:
 * The PacketReset is valid in all operating modes
 *
 * Addressing:
 * This packet may be sent either to a unicast address of a single camera, or to all cameras on an Ethernet segment
 * via the broadcast address.
 *
 * Response:
 * The camera resets immediately after receipt of this packet. There is no response generated, but the reset can be confirmed
 * by subsequently sending a PacketDiscover.
 */
typedef struct PACKED_ATTRIBUTE { PacketGeneric hdr; } PacketReset;

/**
 * A TimeRequest packet is generated by the host to request a reading of the camera's system time base.
 * The TimeRequest packet is a PacketGeneric with the type field set to PKTT_TIMEREQ.
 *
 * Valid States:
 * The TimeRequest packet is valid in both the Configured and Video states. Higher latency will be
 * present during the Video state due to increased processor and network load.
 *
 * Addressing:
 * This packet may only be sent to the unicast address of a single camera.
 *
 * Response:
 * The camera will read the system time base and respond to the originator of the TimeRequest with a PacketTimer.
 *
 * @see PacketTimer
 */
typedef struct PACKED_ATTRIBUTE { PacketGeneric hdr; } PacketTimeRequest;

/**
 * A PacketFlashRequest is sent by the host to request the contents of one Atmel dataflash page.
 * Each page consists of 264 bytes and is requested by an address in the following format:
 *
 *  The address field Holds the 32 bit starting address:
 *                      \li 11 MSB must be zero
 *                      \li next 12 bits are page address
 *                      \li next 9 bits are start offset within the page (normally zero)
 *
 * If the start offset is non-zero, then the data will wrap back from the end of the page to the beginning.
 * Normally, this is undesirable, so a start offset of zero should be used.
 *
 * The AT45DB161D Atmel Dataflash used in this product has 4096 user-accessible flash pages of 264 bytes each.
 *
 * Valid States:
 * The PacketFlashRequest is only valid in the Configured state due to the processing time required.
 *
 * Addressing:
 * This packet may only be sent to the unicast address of a single camera.
 *
 * Response:
 * The camera will read the requested flash page and reply to the originator of the PacketFlashRequest with a
 * PacketFlashPayload containing the data.
 *
 * @see PacketFlashPayload
 */
typedef struct PACKED_ATTRIBUTE {
    /// Generic Command Packet Headers
    PacketGeneric hdr;

    /// Dataflash page address
    uint32_t address;
} PacketFlashRequest;

/**
 * A PacketFlashPayload may be generated by either the host or the camera.
 * When generated by the camera, it is in response to a PacketFlashRequest and contains the flash data
 * requested by that packet. In this case it will have type PKTT_FLASHDATA.
 *
 * When generated by the host, it is sent to erase & write a page (264 bytes) worth of data into the Atmel dataflash.
 * In this case it will have packet type PKTT_FLASHWRITE.
 *
 * Valid States:
 * A PKTT_FLASHWRITE is only valid in the Configured state due to the processing time required.
 *
 * Addressing:
 * A PKTT_FLASHWRITE packet may only be sent to the unicast address of a single camera.
 *
 * Response:
 * After the flash erase/write cycle has completed, the camera will respond to the originator of the PKTT_FLASHWRITE packet
 * with a PacketStatus indicating success. The host should not send further packets until receiving the PacketStatus.
 */

typedef struct PACKED_ATTRIBUTE {
    /// Generic Command Packet Headers
    PacketGeneric hdr;

    uint32_t address;                       /// 32-bit address as specified in the PacketFlashPayload definition
    uint8_t data[FLASH_PAGE_SIZE];          /// Array of 264 bytes of flash data. Index 0 maps to the 1st byte in the page.
} PacketFlashPayload;

/**
 * A PacketTrigControl will be generated by the host in order to set the type of trigger to use for a subsequent video stream.
 * The change will not affect a stream in process; it is only activated at the reception of the next PacketVidStart.
 *
 * Currently valid trigger types are Internal (0) and External (1).
 *
 * Valid States:
 * PacketTrigControl is valid in both Configured and Video states, but a change of trigger state will not take immediate effect
 * when the camera is already in the Video state.
 *
 * Addressing:
 * A PKTT_FLASHWRITE packet may only be sent to the unicast address of a single camera.
 *
 * Response:
 * After the trigger type has been changed, the camera will respond to the originator of the PacketTrigControl with a Status
 * packet indicating success.
 */
typedef struct PACKED_ATTRIBUTE {
    /// Generic Command Packet Headers
    PacketGeneric hdr;

    /// Trigger state configuration
    uint32_t trig_state;
} PacketTrigControl;

/**
 * A PacketSensorRequest is generated by the host to request the 16-bit value of a specific image sensor I2C register.
 *
 * Valid States:
 * PacketTrigControl is valid in both Configured and Video states. However, the Host should pace its requests during Video
 * mode to avoid exceeding the available processor time and network bandwidth.
 *
 * Addressing:
 * A PacketSensorRequest packet may only be sent to the unicast address of a single camera.
 *
 * Response:
 * Once the camera has read the requested value, it will return a PacketSensorData packet to the host. This packet
 * will contain the 16-bit data.
 *
 * @see PacketSensorData
 */
typedef struct PACKED_ATTRIBUTE {
    /// Generic Command Packet Headers
    PacketGeneric hdr;

    /// 8-bit I2C Sensor Address (data returned will be 16-bit)
    uint8_t address;
} PacketSensorRequest;

/**
 * A PacketSensorData may be generated by either the host or the camera. When generated by the camera it will have a type of
 * PKTT_SENSORDATA and will contain the 16-bit data 'data' read from 8-bit image sensor I2C address 'address'.
 *
 * When generated by the host, the type PKTT_SENSORWR will be assigned. A PKTT_SENSORWR packet is used to instruct the camera to
 * write a 16-bit value ('data') into a location on the image sensor I2C bus specified by the 8-bit address 'address'.
 *
 * Valid States:
 * PacketTrigControl is valid in both Configured and Video states. However, the Host should pace its requests during Video
 * mode to avoid exceeding the available processor time and network bandwidth.
 *
 * Addressing:
 * A PKTT_SENSORWR packet may only be sent to the unicast address of a single camera.
 *
 * Response:
 * Once the camera has written the requested value, it will return a PacketStatus to the originating host to indicate success.
 *
 */
typedef struct PACKED_ATTRIBUTE {
    /// Generic Command Packet Headers
    PacketGeneric hdr;

    uint8_t address;            ///< 8-bit I2C Sensor Address
    uint16_t data;              ///< Data payload
} PacketSensorData;

/**
 * A PacketSensorSelect is generated by the host to specify the address of one of the I2C registers to be automatically
 * read once per video frame. In this product, a maximum of four registers can be read.
 *
 * This packet can also be used to disable reading of a particular index position by specifying an address of I2C_AUTO_REG_UNUSED ((uint32_t)-1).
 *
 * Valid States:
 * PacketSensorSelect is valid in both Configured and Video states. However, in Video mode the value of the automatically read
 * register at 'index' is undefined between the time the PacketSensorSelect is sent and the end of the frame after the PacketStatus response is received.
 *
 * Addressing:
 * A PacketSensorSelect packet may only be sent to the unicast address of a single camera.
 *
 * Response:
 * Once the camera has configured the requested value, it will return a PacketStatus to the originating host to indicate success.
 */
typedef struct PACKED_ATTRIBUTE {
    /// Generic Command Packet Headers
    PacketGeneric hdr;

    uint8_t index;          ///< The index of the register in the EOF packet (range 0..3)
    uint32_t address;       ///< The address of the register to read (range 0..255), or I2C_AUTO_REG_UNUSED
} PacketSensorSelect;

/**
 * A PacketImagerMode packet is generated by the host to request a change in image sensor mode (resolution, framerate and binning) to one in a predefined list.
 * In this product, there are 10 modes (0..9). The default mode after a reset is mode 2 (640x480x30fps). The specifics of these modes are defined elsewhere.
 *
 * Valid States:
 * The host may only generate a PacketImagerMode packet when the system is in the Configured state. Changing video mode while streaming is not allowed.
 *
 * Addressing:
 * A PacketSensorSelect packet may only be sent to the unicast address of a single camera.
 *
 * Response:
 * Once the camera has configured the requested mode, it will return a PacketStatus to the originating host to indicate success.
 */
typedef struct PACKED_ATTRIBUTE {
    /// Generic Command Packet Headers
    PacketGeneric hdr;

    uint32_t mode;          ///< The mode number to select (range 0..9)
} PacketImagerMode;



/**
 * The PacketImagerSetRes packet is used only when configuring non-standard image modes. It allows the host to manually configure the camera firmware for
 * arbitrary image dimensions. The camera firmware must always be configured to match the dimensions of the frame coming out of the imager; if the camera
 * is incorrectly configured, system failure could result.
 *
 * In any case, the maximum value of the horizontal parameter is 752 bytes.
 *
 * Valid States:
 * The host may only send a PacketImagerSetRes packet when the camera is in the Configured state. Changing imager resolution while streaming is not allowed.
 *
 * Addressing:
 * A PacketImagerSetRes packet may only be sent to the unicast address of a single camera.
 *
 * Response:
 * Once the camera has configured the requested resolution, it will return a PacketStatus to the originating host to indicate success.
 */
typedef struct PACKED_ATTRIBUTE {
    /// Generic Command Packet Headers
    PacketGeneric hdr;

    uint16_t horizontal;        ///< Number of 8-bit video pixels per image row
    uint16_t vertical;          ///< Number of video lines per frame
} PacketImagerSetRes;

/**
 * A PacketSysConfig is a one-time use packet generated by a host after a unit's initial programming.
 * It specifies the permanent unique MAC address and serial number for a camera.
 *
 * Valid States:
 * The host may only send a PacketSysConfig packet when the camera is in the Configured state.
 *
 * Addressing:
 * A PacketImagerSetRes packet may only be sent to the unicast address of a single camera.
 * @warning: Only ONE unprogrammed camera may be on the connected Ethernet segment when this command is sent
 * @warning: Once the MAC and serial number have been set, the setting is permanent within the Atmel Dataflash chip.
 *
 * Response:
 * Once the camera has programmed the MAC and serial, it will return a PacketStatus to the originating host to indicate success.
 * Once the PacketStatus has been received, a PacketReset should be sent for the changes to take effect.
 */
typedef struct PACKED_ATTRIBUTE {
    /// Generic Command Packet Headers
    PacketGeneric hdr;

    MACAddress mac;
    uint32_t serial;
} PacketSysConfig;

/**
 * @defgroup StatusTypes    Complete listing of all possible PacketStatus status_types.
 */
/*@{*/
#define PKT_STATUST_OK      0           ///< Command completed successfully
#define PKT_STATUST_ERROR   1           ///< Command could not be completed. See status_code for details
/*@}*/

/**
 * @defgroup StatusCodes Complete listing of all possible PacketStatus status_codes.
 */
/*@{*/
#define PKT_ERROR_TIMEOUT   0           ///< No valid response was received during the allotted interval
#define PKT_ERROR_SYSERR    1           ///< An internal system error occurred
#define PKT_ERROR_INVALID   2           ///< Packet is not valid in this mode
/*@}*/

/**
 * A PacketStatus is generated by the camera in reply to a command from the host that does not require a detailed response.
 * Normally it will indicate only success or failure. If failure, the type of failure is indicated by the status_code field.
 *
 */
typedef struct PACKED_ATTRIBUTE {
    /// Generic Command Packet Headers
    PacketGeneric hdr;

    uint32_t status_type;               ///< Type of status report (OK, Error, etc) @see StatusTypes
    uint32_t status_code;               ///< Response code (Error type, etc) @see StatusCodes
} PacketStatus;


/**
 * A PacketTimer is generated by the camera in reply to a TimeRequest from the host.
 * It contains the value of the 64-bit system time base, split between the ticks_hi and ticks_lo fields.
 * The system time base is incremented once per clock cycle and is only reset after a hard reset of the camera board.
 *
 * The system time base will roll over to zero after approximately 10,000 years of uptime.
 *
 * An additional field 'ticks_per_sec' is supplied to convert time base clock ticks into seconds. To obtain microseconds
 * from a PacketTimer, first combine ticks_hi and ticks_lo into a single 64-bit value, then divide by (ticks_per_sec/1000000).
 * (Depending on host architecture, different numerical methods may be used to maintain precision)
 *
 */
typedef struct PACKED_ATTRIBUTE {
    /// Generic Command Packet Headers
    PacketGeneric hdr;

    uint32_t ticks_hi;              ///< 32 MSBs of system time base
    uint32_t ticks_lo;              ///< 32 LSBs of system time base

    uint32_t ticks_per_sec;         ///< Number of time base ticks that occur per second
} PacketTimer;

/**
 * The PacketAnnounce is generated by the camera in response to a PacketDiscover or PacketConfigure.
 * It provides complete information about the camera and the versions of its subcomponents.
 *
 * The system's IP address is not explicitly encoded in the PacketAnnounce since it is present in the IP headers.
 */
typedef struct PACKED_ATTRIBUTE {
    /// Generic Command Packet Headers
    PacketGeneric hdr;

    /// Camera Identification Section
    MACAddress mac;                 ///< The unique six-byte IEEE MAC address assigned to this camera
    uint32_t product_id;            ///< The fixed four-byte product ID assigned to the PR2 camera by Willow Garage
    uint32_t ser_no;                ///< The unique four-byte serial number assigned to this camera
    char product_name[40];          ///< The fixed product name assigned to the PR2 camera by Willow Garage. Null terminated string.

    /**
     * FPGA and system board revision, formatted as follows:
     * \li Bits 16..31: Reserved
     * \li Bits  4..15: FPGA revision
     * \li Bits   0..3: Hardware (PCBA) revision
     */
    uint32_t hw_version;

    /**
     * System soft-core processor firmware version number, formatted as follows:
     *  \li Bits 12..31: Reserved
     *  \li Bits  8..11: Image Pipeline HDL Revision Number
     *  \li Bits   0..7: FPGA Schematic Revision Number
     */
    uint32_t fw_version;

    struct in_addr prev_host;       ///@todo Prev_host field is unused

} PacketAnnounce;


/// Line numbers that match this mask are reserved to indicate special packet types.
#define IMAGER_MAGICLINE_MASK 0xFFF0

/// Flags this video packet as being an normal End Of Frame packet
#define IMAGER_LINENO_EOF 0xFFFF

/// Flags this video packet as being a General Error packet
#define IMAGER_LINENO_ERR 0xFFFE

/// Flags this video packet as being an Overflow packet
#define IMAGER_LINENO_OVF 0xFFFD

/// Flags this video packet as being an Abort packet
#define IMAGER_LINENO_ABORT 0xFFFC

/// Flags a frame as Short (used only by the library)
#define IMAGER_LINENO_SHORT 0xFFFB


/**
 * This frame header is added to the beginning of every video line packet.
 */
typedef struct PACKED_ATTRIBUTE {
      uint32_t frame_number;        ///< Frame number as reported by Imager peripheral
      uint16_t line_number;         ///< Frame/line number as reported by Imager peripheral
      uint16_t horiz_resolution;    ///< Number of 8-bit pixels per video line
      uint16_t vert_resolution;     ///< Number of video line packets per frame (not including EOF packet)
} HeaderVideoLine;


/**
 * This packet is a normal line of video.
 *
 * The data field is sized to accomodate the widest frame possible. 
 *
 * Per Willow Garage request, the PacketEOF is a special case of the PacketVideoLine, with the
 * line number field set to IMAGER_LINENO_NOERR (per client request).
 */
typedef struct PACKED_ATTRIBUTE {
	HeaderVideoLine header;
	uint8_t data[752];
} PacketVideoLine;



/// Number of I2C register to read during each video frame interval
#define I2C_REGS_PER_FRAME 4

/**
 * This packet is sent at the end of every normal video frame. It is also generated when
 * an Imager failure (pipeline error or overflow) is detected.
 *
 * Per Willow Garage request, the PacketEOF is a special case of the HeaderVideoLine, with the
 * line number field set to 0xFFFF (IMAGER_LINENO_EOF).
 */
typedef struct PACKED_ATTRIBUTE {
    HeaderVideoLine header;         ///< Standard video line header

    uint32_t ticks_hi;              ///< End time of frame in ticks (MS word of system time base)
    uint32_t ticks_lo;              ///< End time of frame in ticks (LS word of system time base)

    uint32_t ticks_per_sec;         ///< Number of system time base ticks per second

    uint16_t i2c[I2C_REGS_PER_FRAME];   ///< Storage for I2C values read during the frame
    uint32_t i2c_valid;                 ///< Flags that indicate which 'i2c' values were updated during the previous frame
} PacketEOF;


/// Sentinel value that indicates no I2C read should be performed at that index
#define I2C_AUTO_REG_UNUSED ((uint32_t)-1)


#endif //_IPCAM_PACKET_H_
