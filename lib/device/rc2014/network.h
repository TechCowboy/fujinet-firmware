#ifndef NETWORK_H
#define NETWORK_H

#include <esp_timer.h>

#include <string>

#include "bus.h"

#include "EdUrlParser.h"

#include "Protocol.h"
#include "fnjson.h"


/**
 * Number of devices to expose via rc2014, becomes 0x71 to 0x70 + NUM_DEVICES - 1
 */
#define NUM_DEVICES 8

/**
 * The size of rx and tx buffers
 */
#define INPUT_BUFFER_SIZE 65535
#define OUTPUT_BUFFER_SIZE 65535
#define SPECIAL_BUFFER_SIZE 256

class rc2014Network : public virtualDevice
{

public:
    /**
     * Constructor
     */
    rc2014Network();

    /**
     * Destructor
     */
    virtual ~rc2014Network();

    /**
     * The spinlock for the ESP32 hardware timers. Used for interrupt rate limiting.
     */
    portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

    /**
     * Toggled by the rate limiting timer to indicate that the PROCEED interrupt should
     * be pulsed.
     */
    bool interruptProceed = false;

    /**
     * Called for rc2014 Command 'O' to open a connection to a network protocol, allocate all buffers,
     * and start the receive PROCEED interrupt.
     */
    virtual void open();

    /**
     * Called for rc2014 Command 'C' to close a connection to a network protocol, de-allocate all buffers,
     * and stop the receive PROCEED interrupt.
     */
    virtual void close();


    /**
     * rc2014 Write command
     * Write # of bytes specified by aux1/aux2 from tx_buffer out to rc2014. If protocol is unable to return requested
     * number of bytes, return ERROR.
     */
    virtual void write();

    virtual void read();

    /**
     * rc2014 Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
     * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
     * with them locally. Then serialize resulting NetworkStatus object to rc2014.
     */
    virtual void status();

    /**
     * @brief Called to set prefix
     */
    virtual void set_prefix(unsigned short s);

    /**
     * @brief Called to get prefix
     */
    virtual void get_prefix();

    /**
     * @brief called to set login
     */
    virtual void set_login(uint16_t s);

    /**
     * @brief called to set password
     */
    virtual void set_password(uint16_t s);

    /**
     * Check to see if PROCEED needs to be asserted.
     */
    void rc2014net_poll_interrupt();

    /**
     * Process incoming rc2014 command for device 0x7X
     * @param b The incoming command byte
     */
    virtual void rc2014_process(uint32_t commanddata, uint8_t checksum) override;

private:
    /**
     * rc2014Net Response Buffer
     */
    uint8_t response[1024];

    /**
     * rc2014Net Response Length
     */
    uint16_t response_len=0;

    /**
     * The Receive buffer for this N: device
     */
    std::string *receiveBuffer = nullptr;

    /**
     * The transmit buffer for this N: device
     */
    std::string *transmitBuffer = nullptr;

    /**
     * The special buffer for this N: device
     */
    std::string *specialBuffer = nullptr;

    /**
     * The EdUrlParser object used to hold/process a URL
     */
    EdUrlParser *urlParser = nullptr;

    /**
     * Instance of currently open network protocol
     */
    NetworkProtocol *protocol = nullptr;

    /**
     * Network Status object
     */
    NetworkStatus network_status;
    union _status
    {
        struct _statusbits
        {
            bool client_data_available : 1;
            bool client_connected : 1;
            bool client_error : 1;
            bool server_connection_available : 1;
            bool server_error : 1;
        } bits;
        unsigned char byte;
    } statusByte;

    /**
     * Error number, if status.bits.client_error is set.
     */
    uint8_t err; 

    /**
     * ESP timer handle for the Interrupt rate limiting timer
     */
    esp_timer_handle_t rateTimerHandle = nullptr;

    /**
     * Devicespec passed to us, e.g. N:HTTP://WWW.GOOGLE.COM:80/
     */
    std::string deviceSpec;

    /**
     * The currently set Prefix for this N: device, set by rc2014 call 0x2C
     */
    std::string prefix;

    /**
     * The AUX1 value used for OPEN.
     */
    uint8_t open_aux1;

    /**
     * The AUX2 value used for OPEN.
     */
    uint8_t open_aux2;

    /**
     * The Translation mode ORed into AUX2 for READ/WRITE/STATUS operations.
     * 0 = No Translation, 1 = CR<->EOL (Macintosh), 2 = LF<->EOL (UNIX), 3 = CR/LF<->EOL (PC/Windows)
     */
    uint8_t trans_aux2;

    /**
     * Return value for DSTATS inquiry
     */
    uint8_t inq_dstats = 0xFF;

    /**
     * The login to use for a protocol action
     */
    std::string login;

    /**
     * The password to use for a protocol action
     */
    std::string password;

    /**
     * Timer Rate for interrupt timer
     */
    int timerRate = 100;

    /**
     * The channel mode for the currently open rc2014 device. By default, it is PROTOCOL, which passes
     * read/write/status commands to the protocol. Otherwise, it's a special mode, e.g. to pass to
     * the JSON or XML parsers.
     *
     * @enum PROTOCOL Send to protocol
     * @enum JSON Send to JSON parser.
     */
    enum _channel_mode
    {
        PROTOCOL,
        JSON
    } channelMode;

    /**
     * The current receive state, are we sending channel or status data?
     */
    enum _receive_mode
    {
        CHANNEL,
        STATUS
    } receiveMode = CHANNEL;

    /**
     * saved NetworkStatus items
     */
    unsigned char reservedSave = 0;
    unsigned char errorSave = 1;

    /**
     * The fnJSON parser wrapper object
     */
    FNJSON json;

    /**
     * Bytes sent of current JSON query object.
     */
    unsigned short json_bytes_remaining=0;

    /**
     * Instantiate protocol object
     * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
     */
    bool instantiate_protocol();

    /**
     * Start the Interrupt rate limiting timer
     */
    void timer_start();

    /**
     * Stop the Interrupt rate limiting timer
     */
    void timer_stop();

    /**
     * Is this a valid URL? (used to generate ERROR 165)
     */
    bool isValidURL(EdUrlParser *url);

    /**
     * Preprocess a URL given aux1 open mode. This is used to work around various assumptions that different
     * disk utility packages do when opening a device, such as adding wildcards for directory opens.
     *
     * The resulting URL is then sent into EdURLParser to get our URLParser object which is used in the rest
     * of rc2014Network.
     *
     * This function is a mess, because it has to be, maybe we can factor it out, later. -Thom
     */
    bool parseURL();

    /**
     * We were passed a COPY arg from DOS 2. This is complex, because we need to parse the comma,
     * and figure out one of three states:
     *
     * (1) we were passed D1:FOO.TXT,N:FOO.TXT, the second arg is ours.
     * (2) we were passed N:FOO.TXT,D1:FOO.TXT, the first arg is ours.
     * (3) we were passed N1:FOO.TXT,N2:FOO.TXT, get whichever one corresponds to our device ID.
     *
     * DeviceSpec will be transformed to only contain the relevant part of the deviceSpec, sans comma.
     */
    void processCommaFromDevicespec();

    /**
     * Perform the correct read based on value of channelMode
     * @param num_bytes Number of bytes to read.
     * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
     */
    bool read_channel(unsigned short num_bytes);


    /**
     * @brief Perform read of the current JSON channel
     * @param num_bytes Number of bytes to read
     */
    bool read_channel_json(unsigned short num_bytes);


    /**
     * Perform the correct write based on value of channelMode
     * @param num_bytes Number of bytes to write.
     * @return TRUE on error, FALSE on success. Used to emit rc2014net_error or rc2014net_complete().
     */
    bool rc2014Network_write_channel(unsigned short num_bytes);

    /**
     * @brief perform local status commands, if protocol is not bound, based on cmdFrame
     * value.
     */
    void rc2014Network_status_local();

    /**
     * @brief perform channel status commands, if there is a protocol bound.
     */
    void rc2014Network_status_channel();

    /**
     * @brief get JSON status (# of bytes in receive channel)
     */
    bool rc2014Network_status_channel_json(NetworkStatus *ns);

    /**
     * @brief Parse incoming JSON. (must be in JSON channelMode)
     */
    void rc2014_parse_json();

    /**
     * @brief Set JSON query string. (must be in JSON channelMode)
     */
    void rc2014_set_json_query();

    /**
     * @brief set channel mode, JSON or PROTOCOL
     */
    virtual void rc2014_set_channel_mode();



    /**
     * Called to pulse the PROCEED interrupt, rate limited by the interrupt timer.
     */
    void rc2014Network_assert_interrupt();

    /**
     * @brief Perform the inquiry, handle both local and protocol commands.
     * @param inq_cmd the command to check against.
     */
    void do_inquiry(unsigned char inq_cmd);

    /**
     * @brief set translation specified by aux1 to aux2_translation mode.
     */
    void rc2014Network_set_translation();

    /**
     * @brief Set timer rate for PROCEED timer in ms
     */
    void rc2014Network_set_timer_rate();


    /**
     * @brief parse URL and instantiate protocol
     * @param db pointer to devicespecbuf 256 chars
     */
    void parse_and_instantiate_protocol(std::string d);
};

#endif /* NETWORK_H */