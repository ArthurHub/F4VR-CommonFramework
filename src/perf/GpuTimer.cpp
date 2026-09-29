#include "GpuTimer.h"

#include "Tracy.h"

namespace f4cf::perf
{
    namespace
    {
        std::uint64_t ticksToNs(const std::uint64_t from, const std::uint64_t to, const std::uint64_t frequency)
        {
            return to > from ? static_cast<std::uint64_t>(static_cast<double>(to - from) * 1e9 / static_cast<double>(frequency)) : 0;
        }

        double nsToMs(const std::uint64_t ns)
        {
            return static_cast<double>(ns) / 1e6;
        }
    }

    GpuTimer::Frame::~Frame()
    {
        if (_timer) {
            _timer->end();
        }
    }

    void GpuTimer::Frame::mark(Site& site, const char* plotName) const
    {
        if (_timer) {
            _timer->mark(site, plotName);
        }
    }

    GpuTimer::GpuTimer(Site& root, const char* plotName)
        : _root(root),
          _plotName(plotName)
    {}

    GpuTimer::Frame GpuTimer::begin(ID3D11DeviceContext* context)
    {
        if (!context) {
            return Frame(nullptr);
        }
        _context = context;
        collect();

        const bool record = isEnabled();
        const bool plot = isTracyConnected();
        if (_unavailable || (!record && !plot) || _inFlight == SLOTS) {
            return Frame(nullptr);
        }

        if (!_device) {
            // borrowed: the game keeps its device alive
            Microsoft::WRL::ComPtr<ID3D11Device> device;
            context->GetDevice(device.GetAddressOf());
            _device = device.Get();
        }
        Slot& slot = _slots[(_oldest + _inFlight) % SLOTS];
        if (!slot.disjoint) {
            const D3D11_QUERY_DESC desc{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
            if (!_device || FAILED(_device->CreateQuery(&desc, slot.disjoint.GetAddressOf()))) {
                _unavailable = true;
                logger::warn("GPU timing unavailable: could not create a timestamp disjoint query");
                return Frame(nullptr);
            }
        }
        slot.marks = 0;
        slot.record = record;
        slot.plot = plot;
        slot.window = windowStart();

        context->Begin(slot.disjoint.Get());
        _current = &slot;
        if (!issueTimestamp(slot)) {
            // the disjoint query still has to end; the frame is never read back
            context->End(slot.disjoint.Get());
            _current = nullptr;
            return Frame(nullptr);
        }
        return Frame(this);
    }

    /**
     * Read back every frame whose results are in, oldest first, without waiting or flushing. The first one not in yet
     * stops it, since nothing issued after it is in either.
     */
    void GpuTimer::collect()
    {
        while (_inFlight > 0) {
            const Slot& slot = _slots[_oldest];
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            const HRESULT result = _context->GetData(slot.disjoint.Get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (result == S_FALSE) {
                return;
            }
            // a disjoint clock (a power state change, a driver reset) makes the timestamps meaningless
            if (result == S_OK && !disjoint.Disjoint && disjoint.Frequency > 0) {
                readBack(slot, disjoint.Frequency);
            }
            _oldest = (_oldest + 1) % SLOTS;
            --_inFlight;
        }
    }

    /**
     * Turn one frame's timestamps into the root's time and each span's, recorded while perf still records the window
     * the frame was issued in, and plotted while a Tracy viewer is connected.
     */
    void GpuTimer::readBack(const Slot& slot, const std::uint64_t frequency)
    {
        if (slot.marks < 2) {
            return;
        }
        std::array<std::uint64_t, MAX_SPANS + 1> ticks{};
        for (std::size_t i = 0; i < slot.marks; ++i) {
            if (_context->GetData(slot.timestamps[i].Get(), &ticks[i], sizeof(std::uint64_t), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
                return;
            }
        }

        // switched off or reset since the frame was issued: it belongs to no window
        const bool record = slot.record && isEnabled() && slot.window == windowStart();
        const bool plot = slot.plot && isTracyConnected();

        const auto totalNs = ticksToNs(ticks[0], ticks[slot.marks - 1], frequency);
        if (record) {
            _root.record(totalNs, nullptr);
        }
        if (plot && _plotName) {
            tracyPlot(_plotName, nsToMs(totalNs));
        }
        for (std::size_t i = 1; i < slot.marks; ++i) {
            const auto& span = slot.spans[i - 1];
            const auto ns = ticksToNs(ticks[i - 1], ticks[i], frequency);
            if (record) {
                span.site->record(ns, &_root);
            }
            if (plot && span.plotName) {
                tracyPlot(span.plotName, nsToMs(ns));
            }
        }
    }

    /**
     * Issue the slot's next timestamp, creating its query the first time.
     */
    bool GpuTimer::issueTimestamp(Slot& slot)
    {
        auto& query = slot.timestamps[slot.marks];
        if (!query) {
            const D3D11_QUERY_DESC desc{ D3D11_QUERY_TIMESTAMP, 0 };
            if (FAILED(_device->CreateQuery(&desc, query.GetAddressOf()))) {
                _unavailable = true;
                logger::warn("GPU timing unavailable: could not create a timestamp query");
                return false;
            }
        }
        _context->End(query.Get());
        ++slot.marks;
        return true;
    }

    void GpuTimer::mark(Site& site, const char* plotName)
    {
        if (!_current || _current->marks > MAX_SPANS) {
            return;
        }
        _current->spans[_current->marks - 1] = { &site, plotName };
        (void)issueTimestamp(*_current);
    }

    /**
     * End the frame being issued: the disjoint query closes around its timestamps, and the slot is in flight.
     */
    void GpuTimer::end()
    {
        if (!_current) {
            return;
        }
        _context->End(_current->disjoint.Get());
        _current = nullptr;
        ++_inFlight;
    }
}
