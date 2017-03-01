//! \file
//! \brief Serial interface for platform drivers with listen mode support.
//! \todo Find better place (and probably name?) for current header and class.
#ifndef DEV_BUS_SERIAL_HPP_
#define DEV_BUS_SERIAL_HPP_

#include <ecl/err.hpp>
#include <ecl/assert.h>
#include <common/bus.hpp>
#include <ecl/thread/semaphore.hpp>

#include <atomic>

namespace ecl
{

//! Serial driver interface.
//! \details The serial allows to abstract async,
//! interrupt-driven nature of platform-level drivers and
//! provide synchronus, buffered data management interface.
//! \tparam PBus Exclusevely owned platform bus.
//! \tparam buf_size Size of internal rx and tx buffers.
template<class PBus, size_t buf_size = 128>
class serial
{
public:

    static constexpr auto buffer_size = buf_size;
    using platform_handle = PBus;

    serial() = delete;
    serial(const serial &other) = delete;
    serial(serial &&other) = delete;
    ~serial() = delete;

    //! Initialize serial driver and underlying platform bus.
    //! \pre Driver is not initialized.
    //! \return Operation status.
    static err init();

    //! Deinitialize serial driver and underlying platform bus.
    //! \pre Driver is initialized.
    //! \return Operation status.
    static err deinit();

    //! Obtains a byte from serial device.
    //! \pre Driver is initialized.
    //! \details Call will block if there is no data to return.
    //! Otherwise data will be stored in the given argument and
    //! method will return.
    //! \param[out] byte Place for incoming byte. Will remain
    //!                  unchanged if error occurs.
    //! \return Operation status.
    //! \retval err::nobufs Some data will be lost after the call completes.
    //!
    static err recv_byte(uint8_t &byte);

    //! Gets buffer from serial device.
    //! \pre Driver is initialized.
    //! \details Call will block if there is no data to return.
    //! Otherwise, buffer will be populated with avaliable data
    //! and method will return.
    //! \param[out]     buf Buffer to fill with data.
    //! \param[in,out]  sz  Size of buffer. Will be updated with
    //!                     amount of bytes written to the buffer.
    //!                     In case of error, size will be updated
    //!                     with bytes successfully stored in the buffer
    //!                     before error occurred.
    //! \return Operation status.
    //! \retval err::nobufs Some data will be lost after the call completes.
    static err recv_buf(uint8_t *buf, size_t &sz);

    //! Sends byte to a serial device.
    //! \pre Driver is initialized.
    //! \details It may not block if internal buffering is applied.
    //! It may also block if platform device is not yet ready to
    //! send data.
    //! \param[in] byte Byte to send.
    //! \return Operation status.
    static err send_byte(uint8_t byte);


    //! Sends buffer to a serial stream.
    //! \pre Driver is initialized.
    //! \details It may not block if internal buffering is applied.
    //! It may also block if platform device is not yet ready to
    //! send data.
    //! \param[in]      buf Buffer to send.
    //! \param[in,out]  sz  Size of buffer. Will be updated with
    //!                     amount of bytes sent In case of error,
    //!                     size will be updated with amount of bytes
    //!                     successfully sent before error occur.
    //! \return Operation status.
    static err send_buf(const uint8_t *buf, size_t &sz);

private:
    static void bus_handler(bus_channel ch, bus_event type, size_t total);

    static err try_start_xfer();

    static uint8_t m_rx_buffer[buf_size];
    static uint8_t m_tx_buffer[buf_size];

    static std::atomic_bool m_rx_new_xfer_allowed;
    static std::atomic_bool m_rx_buffer_empty;

    static std::atomic_size_t m_rx_write_iter; //!< Used for accepting data from the bus
    static std::atomic_size_t m_rx_read_iter; //!< Used for returning data to the user

    static bool m_is_inited;
};

template <class PBus, size_t buf_size>
uint8_t serial<PBus, buf_size>::m_rx_buffer[buf_size];

template <class PBus, size_t buf_size>
uint8_t serial<PBus, buf_size>::m_tx_buffer[buf_size];

template <class PBus, size_t buf_size>
std::atomic_size_t serial<PBus, buf_size>::m_rx_write_iter;

template <class PBus, size_t buf_size>
std::atomic_size_t serial<PBus, buf_size>::m_rx_read_iter;

template <class PBus, size_t buf_size>
bool serial<PBus, buf_size>::m_is_inited;

template <class PBus, size_t buf_size>
std::atomic_bool serial<PBus, buf_size>::m_rx_new_xfer_allowed;

template <class PBus, size_t buf_size>
std::atomic_bool serial<PBus, buf_size>::m_rx_buffer_empty;

template <class PBus, size_t buf_size>
err serial<PBus, buf_size>::init()
{
    ecl_assert(!m_is_inited);
    auto result = PBus::init();
    if (is_error(result)) {
        return result;
    }
    PBus::set_handler(bus_handler);
    result = PBus::enable_listen_mode();
    if (is_error(result)) {
        return result;
    }
    m_rx_new_xfer_allowed = true;
    m_rx_buffer_empty = true;
    PBus::set_rx(m_rx_buffer, buffer_size);
    result = PBus::do_xfer();
    if (is_ok(result)) {
        m_is_inited = true;
    }
    return result;
}

template <class PBus, size_t buf_size>
err serial<PBus, buf_size>::deinit()
{
    ecl_assert(m_is_inited);
    PBus::cancel_xfer();
    PBus::reset_buffers();
    m_rx_read_iter = 0;
    m_rx_write_iter = 0;
    m_is_inited = false;
    return err::ok;
}

template <class PBus, size_t buf_size>
void serial<PBus, buf_size>::bus_handler(bus_channel ch, bus_event type, size_t total)
{
    if (ch == bus_channel::rx) {
        ecl_assert(type == bus_event::tc);
        m_rx_write_iter++;
        ecl_assert(m_rx_write_iter == total);
        m_rx_buffer_empty = false;

        // The buffer has been overflowed.
        // Move read iterator to point to the most relevant data
        //
        // It might be a bit unclear why the same expression
        // m_rx_read_iter == m_rx_write_iter both means that the buffer is
        // overflown and empty (see recv_buf()).
        // However, the context of bus_handler() implies that the
        // read iterator is "approached" by write iterator from behind
        // and we properly adjust the position of read iterator to be
        // always ahead of the write iterator.
        if (m_rx_read_iter == m_rx_write_iter) {
            m_rx_read_iter++;
            if (m_rx_read_iter == buffer_size) {
                m_rx_read_iter = 0;
            }
        }

        // Start new xfer if needed and allowed
        if (m_rx_new_xfer_allowed) {
            try_start_xfer();
        }
    }
    // TODO: handle TX events
}

template <class PBus, size_t buf_size>
err serial<PBus, buf_size>::recv_byte(uint8_t &byte)
{
    size_t sz = 1;
    return recv_buf(&byte, sz);
}

template <class PBus, size_t buf_size>
err serial<PBus, buf_size>::recv_buf(uint8_t *buf, size_t &sz)
{
    ecl_assert(m_is_inited);
    err result = err::ok;

    // Restrict starting new xfer from bus_handler()
    // to prevent data loss
    m_rx_new_xfer_allowed = false;

    // Wait until data is available on the read end
    while ((m_rx_read_iter == m_rx_write_iter && m_rx_buffer_empty)) { }

    // Save atomic variables on the stack
    size_t write_pos = m_rx_write_iter;
    size_t read_pos = m_rx_read_iter;

    // Take into account relative positions of read and write iterators
    sz = std::min(sz, write_pos > read_pos ? write_pos - read_pos
            : buffer_size - read_pos);
    std::copy(m_rx_buffer + read_pos,
            m_rx_buffer + read_pos + sz, buf);

    if (m_rx_read_iter == m_rx_write_iter) {
        m_rx_buffer_empty = true;
    }

    // The read iterator might have been changed by the bus_handler during copying.
    // If it wasn't, it's safe to advance iterator by the number of bytes copied above
    if (!m_rx_read_iter.compare_exchange_weak(read_pos, read_pos + sz)) {
        // If the read iterator has been advanced, notify caller that the data
        // is corrupted (part of the buffer was overwritten by bus_handler)
        result = err::nobufs;
    }

    if (m_rx_read_iter == buffer_size) {
        m_rx_read_iter = 0;
    }

    // Start new xfer if required
    try_start_xfer();

    // Allow starting new xfer from bus_handler()
    m_rx_new_xfer_allowed = true;
    return result;
}

template <class PBus, size_t buf_size>
err serial<PBus, buf_size>::try_start_xfer()
{
    if (m_rx_write_iter == buffer_size) {
        m_rx_write_iter = 0;
        return PBus::do_xfer();
    }
    return err::ok;
}

template <class PBus, size_t buf_size>
err serial<PBus, buf_size>::send_byte(uint8_t byte)
{
    size_t sz = 1;
    return send_buf(&byte, sz);
}

template <class PBus, size_t buf_size>
err serial<PBus, buf_size>::send_buf(const uint8_t *buf, size_t &size)
{
    static_cast<void>(buf);
    static_cast<void>(size);
    return ecl::err::ok;
}

} // namespace ecl

#endif // DEV_BUS_SERIAL_HPP_
