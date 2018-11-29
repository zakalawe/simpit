#include <string>

enum class Key
{
    A = 12,
    B = 11,
    C = 10,
    D = 9,
    E = 8,
    F = 20,
    G = 19,
    H = 18,
    I = 17,
    J = 16,
    K = 28,
    L = 27,
    M = 26,
    N = 25,
    O = 24,
    P = 36,
    Q = 35,
    R = 34,
    S = 33,
    T = 32,
    U = 44,
    V = 43,
    W = 42,
    X = 41,
    Y = 40,
    Z = 52,

    Digit1 = 31,
    Digit2 = 30,
    Digit3 = 29,

    Digit4 = 39,
    Digit5 = 38,
    Digit6 = 37,

    Digit7 = 47,
    Digit8 = 46,
    Digit9 = 45,

    Period = 55,
    Digit0 = 54,
    PlusMinus = 53,

    Space = 51,
    Delete = 50,
    Slash = 49,
    Clear = 48,

    Climb = 7,
    Descent = 69,
    Cruise = 3,
    N1Limit = 14,
    Fix = 13,
    PreviousPage = 22,
    NextPage = 21,
    Program = 1,
    Exec = 0,
    Hold = 2,
    DepArr = 4,
    Legs = 5,
    Menu = 6,
    Route = 15,
    InitRef = 62,

    LSK_L0 = 56,
    LSK_L1 = 57,
    LSK_L2 = 58,
    LSK_L3 = 59,
    LSK_L4 = 60,
    LSK_L5 = 61,

    LSK_R0 = 63,
    LSK_R1 = 64,
    LSK_R2 = 65,
    LSK_R3 = 66,
    LSK_R4 = 67,
    LSK_R5 = 68,

    NumKeys = 70
};

std::string codeForKey(Key k);
