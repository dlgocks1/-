package com.alife.protect_chichi.Service

import retrofit2.Call
import retrofit2.http.Body
import retrofit2.http.GET
import retrofit2.http.POST

interface GetHeartRetrofitInterface {
    @POST("/default/RDS_to_APIGateway/send")
    fun getHeartRate(@Body time : String) : Call<GetHeartResponse>

}