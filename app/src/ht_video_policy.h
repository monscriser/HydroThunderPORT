#pragma once
namespace ht::video {
// The swap limit and guest time source are independent. Removing the swap
// limit must never switch the virtual console clock to the old 1000 Hz mode.
constexpr int Rates[]={60,30,40,90,120,0};
constexpr int RateCount=int(sizeof(Rates)/sizeof(Rates[0]));
constexpr int SwapLimit(int selection){return selection>=0&&selection<RateCount?Rates[selection]:60;}
constexpr int ClockRate(int selection){return SwapLimit(selection)>0?SwapLimit(selection):60;}
}
