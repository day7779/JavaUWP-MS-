#pragma once

#include <cstddef>

namespace mousesupport {

enum ButtonAction {
    ButtonRelease = 0,
    ButtonPress = 1,
};

struct ButtonEvent {
    int button;
    int action;
};

struct ConsumeResult {
    double dx;
    double dy;
    double wheel;
    bool hasAbsolute;
    bool absoluteWindow;
    double absX;
    double absY;
    int buttonCount;
    ButtonEvent buttons[16];

    double appliedAgeMicros;
    int sampleCount;
};

class MouseMailbox {
public:
    MouseMailbox() { reset(); }

    void reset() {
        pendingDx_ = 0.0;
        pendingDy_ = 0.0;
        pendingWheel_ = 0.0;
        pendingSamples_ = 0;
        firstPendingMicros_ = 0;
        recvCount_ = 0;
        hasAbs_ = false;
        absWindow_ = false;
        absX_ = 0.0;
        absY_ = 0.0;
        targetButtons_ = 0;
        pendingButtonCount_ = 0;
    }

    long long receivedCount() const { return recvCount_; }
    int depth() const { return pendingSamples_; }

    void submitRelative(long long tMicros, double dx, double dy, double wheel) {
        markPending(tMicros);
        pendingDx_ += dx;
        pendingDy_ += dy;
        pendingWheel_ += wheel;
        ++recvCount_;
    }

    void submitAbsolute(long long tMicros, double x, double y, bool window, double wheel) {
        absX_ = x;
        absY_ = y;
        absWindow_ = window;
        hasAbs_ = true;

        pendingDx_ = 0.0;
        pendingDy_ = 0.0;
        pendingSamples_ = 0;
        firstPendingMicros_ = 0;
        if (wheel != 0.0) {
            markPending(tMicros);
            pendingWheel_ += wheel;
        }
        ++recvCount_;
    }

    void submitButtonValue(int bit, int value) {
        if (value < 0) return;
        const bool wasDown = (targetButtons_ & bit) != 0;
        const bool isDown = value != 0;
        if (wasDown == isDown) return;

        if (isDown) targetButtons_ |= bit;
        else targetButtons_ &= ~bit;

        const int button = mapBitToButton(bit);
        const int capacity = static_cast<int>(sizeof(pendingButtons_) / sizeof(pendingButtons_[0]));
        if (button >= 0 && pendingButtonCount_ < capacity) {
            pendingButtons_[pendingButtonCount_].button = button;
            pendingButtons_[pendingButtonCount_].action = isDown ? ButtonPress : ButtonRelease;
            ++pendingButtonCount_;
        }
    }

    void consume(long long nowMicros, ConsumeResult& out) {
        out.dx = pendingDx_;
        out.dy = pendingDy_;
        out.wheel = pendingWheel_;
        out.hasAbsolute = false;
        out.absoluteWindow = false;
        out.absX = 0.0;
        out.absY = 0.0;
        out.buttonCount = 0;
        out.appliedAgeMicros = firstPendingMicros_ > 0
            ? static_cast<double>(nowMicros - firstPendingMicros_)
            : 0.0;
        out.sampleCount = pendingSamples_;

        pendingDx_ = 0.0;
        pendingDy_ = 0.0;
        pendingWheel_ = 0.0;
        pendingSamples_ = 0;
        firstPendingMicros_ = 0;

        if (hasAbs_) {
            out.hasAbsolute = true;
            out.absoluteWindow = absWindow_;
            out.absX = absX_;
            out.absY = absY_;
            hasAbs_ = false;
        }

        const int maxButtons = static_cast<int>(sizeof(out.buttons) / sizeof(out.buttons[0]));
        out.buttonCount = pendingButtonCount_ < maxButtons ? pendingButtonCount_ : maxButtons;
        for (int i = 0; i < out.buttonCount; ++i) {
            out.buttons[i] = pendingButtons_[i];
        }
        pendingButtonCount_ = 0;
    }

    static int mapBitToButton(int bit) {
        switch (bit) {
        case 1: return 0;
        case 2: return 1;
        case 4: return 2;
        case 8: return 3;
        case 16: return 4;
        default: return -1;
        }
    }

private:
    void markPending(long long tMicros) {
        if (pendingSamples_ == 0) firstPendingMicros_ = tMicros;
        ++pendingSamples_;
    }

    double pendingDx_;
    double pendingDy_;
    double pendingWheel_;
    int pendingSamples_;
    long long firstPendingMicros_;
    long long recvCount_;
    bool hasAbs_;
    bool absWindow_;
    double absX_;
    double absY_;
    int targetButtons_;
    ButtonEvent pendingButtons_[16];
    int pendingButtonCount_;
};

}
