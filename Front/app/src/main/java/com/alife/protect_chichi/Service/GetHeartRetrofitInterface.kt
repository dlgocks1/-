package com.alife.protect_chichi.Service

import retrofit2.Call
import retrofit2.http.Body
import retrofit2.http.GET
import retrofit2.http.POST
import retrofit2.http.Query

interface GetHeartRetrofitInterface {
    @POST("/default/RDS_to_APIGateway/send")
    fun getHeartRate(@Body time : String) : Call<GetHeartResponse>

//    @POST("/default/check_esp32_connect/send")
//    fun sendSignal(@Body desired : String) : Call<GetHeartResponse>

    @GET("/default/check_esp32_connect/send")
    fun sendSignal(@Query("desired") flag : Int) : Call<GetHeartResponse>

    @POST("/default/send_meal_time")
    fun sendfood(@Body time : String) : Call<GetHeartResponse>
}