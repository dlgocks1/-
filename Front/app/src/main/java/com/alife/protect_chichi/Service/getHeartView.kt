package com.alife.protect_chichi.Service


interface getHeartView {
    fun onSuccess(result: GetHeartResponse)
    fun onFailure(code:Int, message:String)
}