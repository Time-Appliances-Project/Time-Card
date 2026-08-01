/*
 * liboscillator-disciplining: Disciplining Algorithm for Orolia's mRO50.
 * Copyright (C) 2021  Spectracom SAS

 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.

 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include <assert.h>
#include <stdio.h>
#include <stdbool.h>

#include "../src/utils.h"
#include "../src/compare_floats.h"

int main(int argc, char *argv[]) {
    int ret;

    printf("TEST utils program\n");

    printf("Testing simple linear reg\n");
    /* Test 1 */
    float x1[4] = { 1.0, 2.0, 4.0, 8.0 };
    float y1[4] = { 30.0, 50.0, 90.0, 170.0 };

    struct linear_func_param test_result;

    ret = simple_linear_reg(x1, y1, 4, &test_result);

    if (ret != 0) {
        printf("Error occured \n");
    } else {
        printf("a = %f\n", test_result.a);
        printf("b = %f\n", test_result.b);
        assert(test_result.a == 20.0);
        assert(test_result.b == 10.0);
        printf("Test passed\n");
    }


    printf("Testing simple linear reg v2\n");
    /* Test 2*/
    float x30[30] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29};
    float y30[30] = { 7.013, 6.646, 12.172, 11.741, 11.138, 11.34, 12.295, 12.665, 12.965, 18.695999, 14.807, 16.389999, 18.638, 18.49, 20.988001, 23.315001, 23.247999, 26.271, 26.343, 24.297001, 28.483999, 29.927, 31.177, 30.107, 28.752001, 30.215, 31.666, 35.004002, 32.941002, 33.147999};

    struct linear_func_param test_result_2;

    ret = simple_linear_reg(x30, y30, 30, &test_result_2);

    if (ret != 0) {
        printf("Error occured \n");
    } else {
        printf("a = %f +/- %f\n", test_result_2.a, test_result_2.a_std);
        printf("b = %f +/- %f\n", test_result_2.b, test_result_2.b_std);
        printf("R2 = %f\n", test_result_2.R2);
        assert(compare_float(test_result_2.a, 0.967053) == 1);
        assert(compare_float(test_result_2.b, 7.340358) == 1);
        assert(compare_float(test_result_2.a_std, 0.036008) == 1);
        assert(compare_float(test_result_2.b_std, 0.6080707) == 1);
        assert(compare_float(test_result_2.R2, 0.962629) == 1);
        printf("Test passed\n");
    }



    printf("Testing lin_interp\n");
    float x2[5] = { 1.0, 2.0, 3.0, 4.0, 5.0 };
    float y2[5] = { 10.0, 8.0, 12.0, 10.0, 9.0};
    float result;

    /* X interpolation */
    ret = lin_interp(x2, y2, 5, true, 1.5, &result);
    assert(ret == 0);
    assert(compare_float(result, 9.0) == 1);

    ret = lin_interp(x2, y2, 5, true, 2.5, &result);
    assert(ret == 0);
    assert(compare_float(result, 10.) == 1);

    ret = lin_interp(x2, y2, 5, true, 3.5, &result);
    assert(ret == 0);
    assert(compare_float(result, 11.) == 1);

    ret = lin_interp(x2, y2, 5, true, 4.5, &result);
    assert(ret == 0);
    assert(compare_float(result, 9.5) == 1);

    /* Y interpolation */
    float x3[5] = { 1.0, 2.0, 3.0, 4.0, 5.0 };
    float y3[5] = { 10.0, 15.0, 25.0, 50.0, 100.0};

    ret = lin_interp(x3, y3, 5, false, 12.5, &result);
    assert(ret == 0);
    assert(compare_float(result, 1.5) == 1);

    ret = lin_interp(x3, y3, 5, false, 75.0, &result);
    assert(ret == 0);
    assert(compare_float(result, 4.5) == 1);

    ret = lin_interp(x3, y3, 5, false, 150.0, &result);
    assert(ret == 0);
    assert(compare_float(result, 6.0) == 1);

    ret = lin_interp(x3, y3, 5, false, 7.5, &result);
    assert(ret == 0);
    assert(compare_float(result, 0.5) == 1);
    printf("Test passed\n");
}
