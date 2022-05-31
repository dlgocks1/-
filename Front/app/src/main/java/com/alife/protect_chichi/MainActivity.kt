package com.alife.protect_chichi

import android.graphics.Color
import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.util.Log
import com.alife.protect_chichi.Service.GetHeartResponse
import com.alife.protect_chichi.Service.GetHeartService
import com.alife.protect_chichi.Service.getHeartView
import com.alife.protect_chichi.databinding.ActivityMainBinding
import com.hookedonplay.decoviewlib.charts.SeriesItem
import com.hookedonplay.decoviewlib.events.DecoEvent
import org.json.JSONException

import org.json.JSONObject

import org.json.JSONArray


class MainActivity : AppCompatActivity(), getHeartView {

    val heartService = GetHeartService()
    private lateinit var binding: ActivityMainBinding
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        heartService.setgetisLikeView(this)

        initView()
        getHeartRate()
        setContentView(binding.root)
    }

    private fun getHeartRate() {
        heartService.getHeartRate("{\"time\":\"2022-05-31 22:31:12\"}")
    }

    private fun initView() {
        binding.mainChart.addSeries(SeriesItem.Builder(Color.parseColor("#20000000"))
            .setRange(0f, 150f, 150f)
            .setInitialVisibility(true)
            .setLineWidth(50f)
            .build())

        val seriesItem1 = SeriesItem.Builder(Color.parseColor("#9b111e"))
            .setRange(0f, 100f, 0f)
            .setLineWidth(50f)
            .build()

        binding.mainChart.configureAngles(360, -50)
        val series1Index: Int = binding.mainChart.addSeries(seriesItem1)
        // 이벤트 넣기
//        binding.homeContentView.addEvent(DecoEvent.Builder(DecoEvent.EventType.EVENT_SHOW, true)
//            .setDelay(1000)
//            .setDuration(2000)
//            .build())
        binding.mainChart.addEvent(DecoEvent.Builder(58f)
            .setIndex(series1Index)
            .build())
    }

    override fun onSuccess(result: GetHeartResponse) {
        Log.d("test", result.body.toString())
        try {
            val jsonArray = JSONArray(result.body)
            val subJsonObject = jsonArray.getJSONObject(jsonArray.length()-1)
            val nowheartRate = subJsonObject.getString("bpm")
            binding.mainHeartTv.text = "${nowheartRate}bpm"
            if(nowheartRate.toInt()<70){
                binding.mainNormalTv.text = "너무 낮아요"
            }else if(nowheartRate.toInt()<120){
                binding.mainNormalTv.text = "정상"
            }else{
                binding.mainNormalTv.text = "너무 높아요"
            }
        } catch (e: JSONException) {
            e.printStackTrace()
        }
    }

    override fun onFailure(code: Int, message: String) {
        Log.d("test", message.toString())
    }
}