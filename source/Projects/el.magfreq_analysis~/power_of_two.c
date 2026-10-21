/**
 * @file power_of_two.c
 * @brief Fast bitwise power of two validation for FFTease in Max 8/9.
 */

int power_of_two(int test)
{
    return (test > 0) && ((test & (test - 1)) == 0);
}