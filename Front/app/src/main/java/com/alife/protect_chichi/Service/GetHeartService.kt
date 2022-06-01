package com.alife.protect_chichi.Service

import android.util.Log
import com.google.gson.Gson
import retrofit2.Call
import retrofit2.Callback
import retrofit2.Response
import retrofit2.Retrofit
import retrofit2.converter.gson.GsonConverterFactory
import retrofit2.converter.scalars.ScalarsConverterFactory

class GetHeartService {
    private lateinit var haertView: getHeartView

    fun setgetisLikeView(item: getHeartView) {
        this.haertView = item
    }


    fun getHeartRate(time : String)
    {
        val gson = Gson()
        val retrofit = Retrofit.Builder()
            .baseUrl("https://lzjitrj3w7.execute-api.ap-northeast-2.amazonaws.com")
            .addConverterFactory(ScalarsConverterFactory.create())
            .addConverterFactory(GsonConverterFactory.create(gson))
            .build()

        val Service = retrofit.create(GetHeartRetrofitInterface::class.java)
        Service.getHeartRate(time).enqueue(object : Callback<GetHeartResponse> {
            override fun onResponse(
                call: Call<GetHeartResponse>,
                response: Response<GetHeartResponse>
            ){
                val resp = response.body()!!
                when (resp.statusCode) {
                    200 -> haertView.onSuccess(resp)
                    else -> {
                        haertView.onFailure(400,resp.toString())
                    }
                }
            }
            override fun onFailure(call: Call<GetHeartResponse>, t: Throwable) {
                haertView.onFailure(400, "네트워크 오류 발생")
            }
        })
    }

    fun sendSignal(desired : Int)
    {
        val gson = Gson()
        val retrofit = Retrofit.Builder()
            .baseUrl("https://yh5xahjhve.execute-api.ap-northeast-2.amazonaws.com")
            .addConverterFactory(ScalarsConverterFactory.create())
            .addConverterFactory(GsonConverterFactory.create(gson))
            .build()

        val Service = retrofit.create(GetHeartRetrofitInterface::class.java)
        Service.sendSignal(desired).enqueue(object : Callback<GetHeartResponse> {
            override fun onResponse(call: Call<GetHeartResponse>, response: Response<GetHeartResponse>) {
                val resp = response.body()!!
                haertView.onSendSignalSuccess(resp)
            }

            override fun onFailure(call: Call<GetHeartResponse>, t: Throwable) {
                haertView.onSendSignalFailure(400, "네트워크 오류 발생")
            }
        })
    }

    fun sendFoodTime(foodtime : String)
    {
        val gson = Gson()
        val retrofit = Retrofit.Builder()
            .baseUrl("https://67htos9kx9.execute-api.ap-northeast-2.amazonaws.com")
            .addConverterFactory(ScalarsConverterFactory.create())
            .addConverterFactory(GsonConverterFactory.create(gson))
            .build()

        val Service = retrofit.create(GetHeartRetrofitInterface::class.java)
        Service.sendfood(foodtime).enqueue(object : Callback<GetHeartResponse> {
            override fun onResponse(call: Call<GetHeartResponse>, response: Response<GetHeartResponse>) {
                val resp = response.body()!!
                haertView.onSendFoodTimeSuccess(resp)
            }

            override fun onFailure(call: Call<GetHeartResponse>, t: Throwable) {
                haertView.onSendFoodTimeFailure(400, "네트워크 오류 발생")
            }
        })
    }

}